/**************************************************************************/
/*  export_plugin.cpp                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "export_plugin.h"

#include "logo_svg.gen.h"
#include "run_icon_svg.gen.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/io/ip.h"
#include "core/os/os.h"
#include "editor/editor_node.h"
#include "editor/export/editor_export.h"
#include "editor/file_system/editor_paths.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/resources/image_texture.h"

#ifdef MODULE_SVG_ENABLED
#include "modules/svg/image_loader_svg.h"
#endif

static const uint32_t ROMFS_EMPTY = 0xFFFFFFFF;
static const uint32_t SMDH_SIZE = 0x36C0;

static void _put16(uint8_t *p_dst, uint16_t p_v) {
	p_dst[0] = p_v & 0xFF;
	p_dst[1] = p_v >> 8;
}

static void _put32(uint8_t *p_dst, uint32_t p_v) {
	for (int i = 0; i < 4; i++) {
		p_dst[i] = (p_v >> (i * 8)) & 0xFF;
	}
}

static void _put64(uint8_t *p_dst, uint64_t p_v) {
	for (int i = 0; i < 8; i++) {
		p_dst[i] = (p_v >> (i * 8)) & 0xFF;
	}
}

static uint32_t _get32(const uint8_t *p_src) {
	return p_src[0] | (p_src[1] << 8) | (p_src[2] << 16) | ((uint32_t)p_src[3] << 24);
}

// UTF-16LE into a fixed field, truncated, zero padded.
static void _put_utf16(uint8_t *p_dst, int p_max_chars, const String &p_text) {
	Char16String s = p_text.utf16();
	int n = MIN(s.length(), p_max_chars - 1);
	for (int i = 0; i < n; i++) {
		_put16(p_dst + i * 2, s[i]);
	}
}

// Icons in an SMDH are RGB565 in 8x8 tiles, Morton order inside a tile.
static void _put_icon(uint8_t *p_dst, const Ref<Image> &p_icon, int p_size) {
	Ref<Image> img = p_icon->duplicate();
	img->convert(Image::FORMAT_RGBA8);
	img->resize(p_size, p_size, Image::INTERPOLATE_LANCZOS);
	const uint8_t *src = img->ptr();
	for (int y = 0; y < p_size; y++) {
		for (int x = 0; x < p_size; x++) {
			const uint8_t *px = src + (y * p_size + x) * 4;
			// No alpha in SMDH icons: blend over black.
			float a = px[3] / 255.0f;
			uint32_t r = (uint32_t)(px[0] * a), g = (uint32_t)(px[1] * a), b = (uint32_t)(px[2] * a);
			uint16_t c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
			uint32_t tx = x & 7, ty = y & 7;
			uint32_t morton = (tx & 1) | ((ty & 1) << 1) | ((tx & 2) << 1) | ((ty & 2) << 2) | ((tx & 4) << 2) | ((ty & 4) << 3);
			uint32_t idx = (((y >> 3) * (p_size >> 3) + (x >> 3)) << 6) + morton;
			_put16(p_dst + idx * 2, c);
		}
	}
}

Vector<uint8_t> EditorExportPlatform3DS::make_smdh(const String &p_title, const String &p_description, const String &p_author, const Ref<Image> &p_icon) {
	Vector<uint8_t> smdh;
	smdh.resize(SMDH_SIZE);
	uint8_t *d = smdh.ptrw();
	memset(d, 0, SMDH_SIZE);
	memcpy(d, "SMDH", 4);
	// Same text for all 16 languages: short (0x40 chars), long (0x80), publisher (0x40).
	for (int lang = 0; lang < 16; lang++) {
		uint8_t *t = d + 0x8 + lang * 0x200;
		_put_utf16(t, 0x40, p_title);
		_put_utf16(t + 0x80, 0x80, p_description);
		_put_utf16(t + 0x180, 0x40, p_author);
	}
	_put32(d + 0x2018, 0x7FFFFFFF); // Region free.
	_put32(d + 0x2028, 0x0001 | 0x0004); // Visible, allows 3D.
	if (p_icon.is_valid() && !p_icon->is_empty()) {
		_put_icon(d + 0x2040, p_icon, 24);
		_put_icon(d + 0x24C0, p_icon, 48);
	}
	return smdh;
}

// libctru's romfs_dev hash for directory/file lookup.
static uint32_t _romfs_hash(uint32_t p_parent, const Char16String &p_name, uint32_t p_total) {
	uint32_t hash = p_parent ^ 123456789;
	for (int i = 0; i < p_name.length(); i++) {
		hash = (hash >> 5) | (hash << 27);
		hash ^= (uint16_t)p_name[i];
	}
	return hash % p_total;
}

Vector<uint8_t> EditorExportPlatform3DS::make_romfs_header(const String &p_file_name, uint64_t p_file_size, uint32_t &r_data_offset) {
	// Level-3 RomFS (what libctru reads from a .3dsx) with a root directory
	// holding one file.
	Char16String name = p_file_name.utf16();
	uint32_t name_bytes = name.length() * 2;
	uint32_t name_padded = (name_bytes + 3) & ~3;

	const uint32_t header_size = 0x28;
	const uint32_t dir_hash_off = header_size;
	const uint32_t dir_hash_size = 4;
	const uint32_t dir_table_off = dir_hash_off + dir_hash_size;
	const uint32_t dir_table_size = 0x18;
	const uint32_t file_hash_off = dir_table_off + dir_table_size;
	const uint32_t file_hash_size = 4;
	const uint32_t file_table_off = file_hash_off + file_hash_size;
	const uint32_t file_table_size = 0x20 + name_padded;
	const uint32_t data_off = (file_table_off + file_table_size + 0xF) & ~0xF;

	Vector<uint8_t> out;
	out.resize(data_off);
	uint8_t *d = out.ptrw();
	memset(d, 0, data_off);

	_put32(d + 0x00, header_size);
	_put32(d + 0x04, dir_hash_off);
	_put32(d + 0x08, dir_hash_size);
	_put32(d + 0x0C, dir_table_off);
	_put32(d + 0x10, dir_table_size);
	_put32(d + 0x14, file_hash_off);
	_put32(d + 0x18, file_hash_size);
	_put32(d + 0x1C, file_table_off);
	_put32(d + 0x20, file_table_size);
	_put32(d + 0x24, data_off);

	// Directory hash table: the root (offset 0) in its only bucket.
	_put32(d + dir_hash_off, 0);
	// Root directory entry.
	uint8_t *root = d + dir_table_off;
	_put32(root + 0x00, 0); // parent (itself)
	_put32(root + 0x04, ROMFS_EMPTY); // sibling
	_put32(root + 0x08, ROMFS_EMPTY); // first child dir
	_put32(root + 0x0C, 0); // first file
	_put32(root + 0x10, ROMFS_EMPTY); // next in hash bucket
	_put32(root + 0x14, 0); // name length

	// File hash table: one bucket, hash computed as libctru will.
	(void)_romfs_hash(0, name, 1);
	_put32(d + file_hash_off, 0);
	uint8_t *file = d + file_table_off;
	_put32(file + 0x00, 0); // parent dir
	_put32(file + 0x04, ROMFS_EMPTY); // sibling
	_put64(file + 0x08, 0); // data offset (from data_off)
	_put64(file + 0x10, p_file_size);
	_put32(file + 0x18, ROMFS_EMPTY); // next in hash bucket
	_put32(file + 0x1C, name_bytes);
	for (int i = 0; i < name.length(); i++) {
		_put16(file + 0x20 + i * 2, name[i]);
	}

	r_data_offset = data_off;
	return out;
}

void EditorExportPlatform3DS::get_preset_features(const Ref<EditorExportPreset> &p_preset, List<String> *r_features) const {
	// VRAM-compressed textures import as BCn. With textures/etc1 on, the
	// exporter re-encodes them as ETC1; otherwise the template decompresses
	// them at load (bcdec), since the PICA200 can't sample S3TC/ETC2/ASTC.
	r_features->push_back("s3tc");
	r_features->push_back("arm32");
}

void EditorExportPlatform3DS::get_platform_features(List<String> *r_features) const {
	r_features->push_back("3ds");
	r_features->push_back("mobile");
	r_features->push_back(get_os_name().to_lower());
}

void EditorExportPlatform3DS::get_export_options(List<ExportOption> *r_options) const {
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "custom_template/debug", PROPERTY_HINT_GLOBAL_FILE, "*.3dsx"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "custom_template/release", PROPERTY_HINT_GLOBAL_FILE, "*.3dsx"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "application/title", PROPERTY_HINT_PLACEHOLDER_TEXT, "Project name"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "application/description"), "Made with Godot Engine"));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "application/author"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "application/icon", PROPERTY_HINT_FILE, "*.png,*.svg,*.webp,*.jpg"), ""));
	// Re-encode textures as ETC1/ETC1A4, which the PICA200 samples directly
	// (see etc1_export.h). Default: textures imported as VRAM Compressed.
	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "textures/etc1", PROPERTY_HINT_ENUM, "Disabled,VRAM Compressed Only,All"), 1));
	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "cia/unique_id", PROPERTY_HINT_RANGE, "0,1015807,1"), 0)); // 0 = derived from the title.
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "cia/product_code", PROPERTY_HINT_PLACEHOLDER_TEXT, "CTR-P-XXXX"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "cia/banner", PROPERTY_HINT_FILE, "*.png"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "cia/banner_audio", PROPERTY_HINT_FILE, "*.wav"), ""));
}

String EditorExportPlatform3DS::_get_template(const Ref<EditorExportPreset> &p_preset, bool p_debug, String *r_error) const {
	String custom = p_preset->get(p_debug ? "custom_template/debug" : "custom_template/release");
	if (!custom.is_empty()) {
		if (!FileAccess::exists(custom)) {
			if (r_error) {
				*r_error = vformat(TTR("Custom template not found: %s"), custom);
			}
			return String();
		}
		return custom;
	}
	return find_export_template(p_debug ? "3ds_debug.3dsx" : "3ds_release.3dsx", r_error);
}

bool EditorExportPlatform3DS::has_valid_export_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error, bool &r_missing_templates, bool p_debug) const {
	String err;
	bool dvalid = !_get_template(p_preset, true, &err).is_empty();
	bool rvalid = !_get_template(p_preset, false, &err).is_empty();
	r_missing_templates = !dvalid || !rvalid;
	bool valid = p_debug ? dvalid : rvalid;
	if (!valid) {
		r_error = err + "\n" + TTR("Build the templates with platform/3ds/build.sh and install them with platform/3ds/install_templates.sh.") + "\n";
	}
	return valid;
}

bool EditorExportPlatform3DS::has_valid_project_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error) const {
	return true;
}

List<String> EditorExportPlatform3DS::get_binary_extensions(const Ref<EditorExportPreset> &p_preset) const {
	List<String> list;
	list.push_back("3dsx");
	list.push_back("cia");
	return list;
}

String EditorExportPlatform3DS::_get_title(const Ref<EditorExportPreset> &p_preset) const {
	String title = p_preset->get("application/title");
	if (title.is_empty()) {
		title = GLOBAL_GET("application/config/name");
	}
	return title.is_empty() ? String("Godot Game") : title;
}

Ref<Image> EditorExportPlatform3DS::_get_icon(const Ref<EditorExportPreset> &p_preset) const {
	String path = p_preset->get("application/icon");
	if (path.is_empty()) {
		path = GLOBAL_GET("application/config/icon");
	}
	Ref<Image> img;
	if (!path.is_empty()) {
		img.instantiate();
		if (img->load(ProjectSettings::get_singleton()->globalize_path(path)) != OK || img->is_empty()) {
			img.unref();
		}
	}
	if (img.is_null()) {
		// Godot's own icon via the editor theme logo, else a plain tile.
		img = Image::create_empty(48, 48, false, Image::FORMAT_RGBA8);
		img->fill(Color(0.28, 0.55, 0.75));
	}
	return img;
}

Error EditorExportPlatform3DS::_export_3dsx(const Ref<EditorExportPreset> &p_preset, const String &p_template, const String &p_pck, const String &p_path) {
	Vector<uint8_t> tpl = FileAccess::get_file_as_bytes(p_template);
	if (tpl.size() < 0x20 || memcmp(tpl.ptr(), "3DSX", 4) != 0) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Not a 3DSX template: %s"), p_template));
		return ERR_FILE_CORRUPT;
	}
	const uint8_t *t = tpl.ptr();
	uint32_t header_size = t[4] | (t[5] << 8);
	// Code, data and relocations run from the header to the template's SMDH
	// (or the end of the file); keep them byte for byte.
	uint32_t body_start = header_size;
	uint32_t body_end = tpl.size();
	if (header_size >= 0x2C) {
		uint32_t smdh_off = _get32(t + 0x20);
		uint32_t fs_off = _get32(t + 0x28);
		if (smdh_off) {
			body_end = MIN(body_end, smdh_off);
		}
		if (fs_off) {
			body_end = MIN(body_end, fs_off);
		}
	}

	Ref<FileAccess> pck = FileAccess::open(p_pck, FileAccess::READ);
	ERR_FAIL_COND_V(pck.is_null(), ERR_CANT_OPEN);
	uint64_t pck_size = pck->get_length();

	Vector<uint8_t> smdh = make_smdh(_get_title(p_preset), p_preset->get("application/description"), p_preset->get("application/author"), _get_icon(p_preset));
	uint32_t romfs_data_off = 0;
	Vector<uint8_t> romfs = make_romfs_header("game.pck", pck_size, romfs_data_off);

	uint8_t header[0x2C];
	memcpy(header, t, 0x20);
	_put16(header + 4, 0x2C);
	uint32_t smdh_off = 0x2C + (body_end - body_start);
	uint32_t fs_off = (smdh_off + SMDH_SIZE + 3) & ~3;
	_put32(header + 0x20, smdh_off);
	_put32(header + 0x24, SMDH_SIZE);
	_put32(header + 0x28, fs_off);

	Ref<FileAccess> out = FileAccess::open(p_path, FileAccess::WRITE);
	if (out.is_null()) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Can't write %s."), p_path));
		return ERR_CANT_CREATE;
	}
	out->store_buffer(header, 0x2C);
	out->store_buffer(t + body_start, body_end - body_start);
	out->store_buffer(smdh.ptr(), smdh.size());
	while (out->get_position() < fs_off) {
		out->store_8(0);
	}
	out->store_buffer(romfs.ptr(), romfs.size());
	Vector<uint8_t> chunk;
	chunk.resize(1 << 20);
	uint64_t left = pck_size;
	while (left > 0) {
		uint64_t n = pck->get_buffer(chunk.ptrw(), MIN((uint64_t)chunk.size(), left));
		if (n == 0) {
			return ERR_FILE_CORRUPT;
		}
		out->store_buffer(chunk.ptr(), n);
		left -= n;
	}
	return OK;
}

static void _write_silence_wav(const String &p_path) {
	// 0.5 s of mono 22050 Hz silence for the HOME menu banner.
	const uint32_t samples = 11025;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::WRITE);
	ERR_FAIL_COND(f.is_null());
	f->store_buffer((const uint8_t *)"RIFF", 4);
	f->store_32(36 + samples * 2);
	f->store_buffer((const uint8_t *)"WAVEfmt ", 8);
	f->store_32(16);
	f->store_16(1);
	f->store_16(1);
	f->store_32(22050);
	f->store_32(22050 * 2);
	f->store_16(2);
	f->store_16(16);
	f->store_buffer((const uint8_t *)"data", 4);
	f->store_32(samples * 2);
	for (uint32_t i = 0; i < samples; i++) {
		f->store_16(0);
	}
}

static const char *CIA_RSF = R"(BasicInfo:
  Title                   : "@TITLE@"
  CompanyCode             : "00"
  ProductCode             : @PRODUCT@
  ContentType             : Application
  Logo                    : Nintendo

TitleInfo:
  UniqueId                : @UNIQUE_ID@
  Category                : Application

Option:
  UseOnSD                 : true
  FreeProductCode         : true
  MediaFootPadding        : false
  EnableCrypt             : false
  EnableCompress          : true

AccessControlInfo:
  CoreVersion                   : 2
  DescVersion                   : 2
  MemoryType                    : Application
  SystemMode                    : 64MB
  SystemModeExt                 : 124MB
  CpuSpeed                      : 804MHz
  EnableL2Cache                 : true
  CanAccessCore2                : true
  IdealProcessor                : 0
  AffinityMask                  : 1
  Priority                      : 16
  MaxCpu                        : 0x9E
  HandleTableSize               : 0x200
  DisableDebug                  : false
  EnableForceDebug              : false
  CanWriteSharedPage            : true
  CanUsePrivilegedPriority      : false
  CanUseNonAlphabetAndNumber    : true
  PermitMainFunctionArgument    : true
  CanShareDeviceMemory          : true
  RunnableOnSleep               : false
  SpecialMemoryArrange          : true
  ResourceLimitCategory         : Application
  IORegisterMapping:
   - 1ff00000-1ff7ffff:r
  MemoryMapping:
   - 1f000000-1f5fffff:r
  FileSystemAccess:
   - DirectSdmc
  ServiceAccessControl:
   - APT:U
   - ac:u
   - am:net
   - boss:U
   - cam:u
   - cecd:u
   - cfg:nor
   - cfg:u
   - csnd:SND
   - dsp::DSP
   - frd:u
   - fs:USER
   - gsp::Gpu
   - gsp::Lcd
   - hid:USER
   - http:C
   - ir:rst
   - ir:u
   - ir:USER
   - mic:u
   - ndm:u
   - news:s
   - nwm::EXT
   - nwm::UDS
   - ptm:sysm
   - ptm:u
   - pxi:dev
   - soc:U
   - ssl:C
   - y2r:u
  SystemCallAccess:
    ArbitrateAddress: 34
    Break: 60
    CancelTimer: 28
    ClearEvent: 25
    ClearTimer: 29
    CloseHandle: 35
    ConnectToPort: 45
    ControlMemory: 1
    CreateAddressArbiter: 33
    CreateEvent: 23
    CreateMemoryBlock: 30
    CreateMutex: 19
    CreateSemaphore: 21
    CreateThread: 8
    CreateTimer: 26
    DuplicateHandle: 39
    ExitProcess: 3
    ExitThread: 9
    GetCurrentProcessorNumber: 17
    GetHandleInfo: 41
    GetProcessId: 53
    GetProcessIdOfThread: 54
    GetResourceLimit: 56
    GetResourceLimitCurrentValues: 58
    GetResourceLimitLimitValues: 57
    GetSystemInfo: 42
    GetSystemTick: 40
    GetThreadContext: 59
    GetThreadId: 55
    GetThreadIdealProcessor: 15
    GetThreadPriority: 11
    MapMemoryBlock: 31
    OutputDebugString: 61
    QueryMemory: 2
    ReleaseMutex: 20
    ReleaseSemaphore: 22
    SendSyncRequest1: 46
    SendSyncRequest2: 47
    SendSyncRequest3: 48
    SendSyncRequest4: 49
    SendSyncRequest: 50
    SetThreadPriority: 12
    SetTimer: 27
    SignalEvent: 24
    SleepThread: 10
    UnmapMemoryBlock: 32
    WaitSynchronization1: 36
    WaitSynchronizationN: 37

RomFs:
  RootPath: romfs

SystemControlInfo:
  SaveDataSize: 0KB
  RemasterVersion: 0
  StackSize: 0x100000
)";

Error EditorExportPlatform3DS::_export_cia(const Ref<EditorExportPreset> &p_preset, const String &p_template, const String &p_pck, const String &p_path) {
	String makerom = EDITOR_GET("export/3ds/makerom");
	String bannertool = EDITOR_GET("export/3ds/bannertool");
	if (makerom.is_empty() || !FileAccess::exists(makerom) || bannertool.is_empty() || !FileAccess::exists(bannertool)) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), TTR("CIA export needs makerom and bannertool: set Editor Settings > Export > 3DS > Makerom / Bannertool."));
		return ERR_UNCONFIGURED;
	}
	// makerom builds from the ELF that sits next to the .3dsx template.
	String elf = p_template.get_basename() + ".elf";
	if (!FileAccess::exists(elf)) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("CIA export needs the template ELF next to the .3dsx: %s"), elf));
		return ERR_FILE_NOT_FOUND;
	}

	String dir = EditorPaths::get_singleton()->get_temp_dir().path_join("3ds_cia");
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (da->dir_exists(dir)) {
		Ref<DirAccess> old = DirAccess::open(dir);
		if (old.is_valid()) {
			old->erase_contents_recursive();
		}
	}
	da->make_dir_recursive(dir.path_join("romfs"));
	da->copy(p_pck, dir.path_join("romfs/game.pck"));
	da->copy(elf, dir.path_join("game.elf"));

	String title = _get_title(p_preset);
	Vector<uint8_t> smdh = make_smdh(title, p_preset->get("application/description"), p_preset->get("application/author"), _get_icon(p_preset));
	{
		Ref<FileAccess> f = FileAccess::open(dir.path_join("icon.smdh"), FileAccess::WRITE);
		ERR_FAIL_COND_V(f.is_null(), ERR_CANT_CREATE);
		f->store_buffer(smdh.ptr(), smdh.size());
	}

	// Banner: the given image, or the icon centered on a dark plate.
	String banner = p_preset->get("cia/banner");
	Ref<Image> banner_img;
	if (!banner.is_empty()) {
		banner_img.instantiate();
		if (banner_img->load(ProjectSettings::get_singleton()->globalize_path(banner)) != OK) {
			banner_img.unref();
		}
	}
	if (banner_img.is_null()) {
		banner_img = Image::create_empty(256, 128, false, Image::FORMAT_RGBA8);
		banner_img->fill(Color(0.125, 0.15, 0.2));
		Ref<Image> icon = _get_icon(p_preset)->duplicate();
		icon->convert(Image::FORMAT_RGBA8);
		icon->resize(96, 96, Image::INTERPOLATE_LANCZOS);
		banner_img->blend_rect(icon, Rect2i(0, 0, 96, 96), Point2i(80, 16));
	}
	banner_img->convert(Image::FORMAT_RGBA8);
	banner_img->resize(256, 128, Image::INTERPOLATE_LANCZOS);
	banner_img->save_png(dir.path_join("banner.png"));
	String audio = p_preset->get("cia/banner_audio");
	if (!audio.is_empty()) {
		da->copy(ProjectSettings::get_singleton()->globalize_path(audio), dir.path_join("banner.wav"));
	} else {
		_write_silence_wav(dir.path_join("banner.wav"));
	}

	int unique_id = p_preset->get("cia/unique_id");
	if (unique_id <= 0) {
		unique_id = 0x10000 + (int)(title.hash() % 0xE0000);
	}
	String product = p_preset->get("cia/product_code");
	if (product.is_empty()) {
		String letters;
		for (int i = 0; i < title.length() && letters.length() < 4; i++) {
			if (is_ascii_alphanumeric_char(title[i])) {
				letters += String::chr(title[i]).to_upper();
			}
		}
		product = "CTR-P-" + letters;
	}
	String rsf = String(CIA_RSF).replace("@TITLE@", title.replace("\"", "'").substr(0, 63)).replace("@PRODUCT@", product).replace("@UNIQUE_ID@", vformat("0x%05X", unique_id));
	{
		Ref<FileAccess> f = FileAccess::open(dir.path_join("app.rsf"), FileAccess::WRITE);
		ERR_FAIL_COND_V(f.is_null(), ERR_CANT_CREATE);
		f->store_string(rsf);
	}

	String out_text;
	int exit_code = 0;
	List<String> args;
	args.push_back("makebanner");
	args.push_back("-i");
	args.push_back(dir.path_join("banner.png"));
	args.push_back("-a");
	args.push_back(dir.path_join("banner.wav"));
	args.push_back("-o");
	args.push_back(dir.path_join("banner.bnr"));
	Error err = OS::get_singleton()->execute(bannertool, args, &out_text, &exit_code, true);
	if (err != OK || exit_code != 0) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), TTR("bannertool failed:") + "\n" + out_text);
		return ERR_CANT_CREATE;
	}

	// makerom resolves RomFs RootPath against the working directory.
	String cwd = OS::get_singleton()->get_cwd();
	OS::get_singleton()->set_cwd(dir);
	args.clear();
	for (const char *a : { "-f", "cia", "-o", "game.cia", "-elf", "game.elf", "-rsf", "app.rsf", "-icon", "icon.smdh", "-banner", "banner.bnr", "-exefslogo", "-target", "t" }) {
		args.push_back(a);
	}
	out_text = "";
	err = OS::get_singleton()->execute(makerom, args, &out_text, &exit_code, true);
	OS::get_singleton()->set_cwd(cwd);
	if (err != OK || exit_code != 0) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), TTR("makerom failed:") + "\n" + out_text);
		return ERR_CANT_CREATE;
	}
	da->remove(p_path);
	if (da->copy(dir.path_join("game.cia"), p_path) != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Can't write %s."), p_path));
		return ERR_CANT_CREATE;
	}
	add_message(EXPORT_MESSAGE_INFO, TTR("Export"), vformat(TTR("CIA unique id: 0x%05X"), unique_id));
	return OK;
}

Error EditorExportPlatform3DS::export_project(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, BitField<EditorExportPlatform::DebugFlags> p_flags, bool p_notify) {
	ExportNotifier notifier(*this, p_preset, p_debug, p_path, p_flags, p_notify);

	String err_text;
	String tpl = _get_template(p_preset, p_debug, &err_text);
	if (tpl.is_empty()) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Prepare Templates"), err_text);
		return ERR_FILE_NOT_FOUND;
	}

	String tmp = EditorPaths::get_singleton()->get_temp_dir().path_join("3ds_export.pck");
	Error err = save_pack(p_preset, p_debug, tmp);
	if (err != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), TTR("Could not write the project pack."));
		return err;
	}

	if (p_path.get_extension().to_lower() == "cia") {
		err = _export_cia(p_preset, tpl, tmp, p_path);
	} else {
		err = _export_3dsx(p_preset, tpl, tmp, p_path);
	}
	DirAccess::remove_absolute(tmp);
	return err;
}

void EditorExportPlatform3DS::initialize() {
#ifdef MODULE_SVG_ENABLED
	if (EditorNode::get_singleton()) {
		Ref<Image> img = memnew(Image);
		const bool upsample = !Math::is_equal_approx(Math::round(EDSCALE), EDSCALE);
		ImageLoaderSVG::create_image_from_string(img, _3ds_logo_svg, EDSCALE, upsample, false);
		logo = ImageTexture::create_from_image(img);
		ImageLoaderSVG::create_image_from_string(img, _3ds_run_icon_svg, EDSCALE, upsample, false);
		run_icon = ImageTexture::create_from_image(img);
	}
#endif
}

int EditorExportPlatform3DS::get_options_count() const {
	String link = EDITOR_GET("export/3ds/3dslink");
	return (!link.is_empty() && FileAccess::exists(link)) ? 1 : 0;
}

String EditorExportPlatform3DS::get_option_label(int p_index) const {
	return TTR("Run on 3DS");
}

String EditorExportPlatform3DS::get_option_tooltip(int p_index) const {
	return TTR("Sends a debug build to the Homebrew Launcher over Wi-Fi with 3dslink (press Y in the launcher first). Remote debugging connects back to this editor.");
}

Ref<Texture2D> EditorExportPlatform3DS::get_option_icon(int p_index) const {
	return run_icon;
}

// The editor's debugger address defaults to 127.0.0.1, which the console
// can't reach: use this PC's LAN address instead.
static String _lan_address() {
	List<IPAddress> addrs;
	IP::get_singleton()->get_local_addresses(&addrs);
	String fallback;
	for (const IPAddress &a : addrs) {
		if (!a.is_ipv4()) {
			continue;
		}
		String s = String(a);
		if (s.begins_with("192.168.") || s.begins_with("10.")) {
			return s;
		}
		if (s.begins_with("172.")) {
			int second = s.get_slicec('.', 1).to_int();
			if (second >= 16 && second <= 31 && fallback.is_empty()) {
				fallback = s; // Often a container bridge; prefer 192.168/10.
			}
		}
	}
	return fallback;
}

Error EditorExportPlatform3DS::run(const Ref<EditorExportPreset> &p_preset, int p_option, BitField<EditorExportPlatform::DebugFlags> p_debug_flags) {
	String link = EDITOR_GET("export/3ds/3dslink");
	String path = EditorPaths::get_singleton()->get_temp_dir().path_join("godot_run.3dsx");
	Error err = export_project(p_preset, true, path, p_debug_flags);
	if (err != OK) {
		return err;
	}

	Vector<String> flags = gen_export_flags(p_debug_flags);
	String lan = _lan_address();
	for (String &f : flags) {
		if (!lan.is_empty()) {
			f = f.replace("127.0.0.1", lan).replace("localhost", lan);
		}
	}

	List<String> args;
	String address = EDITOR_GET("export/3ds/console_address");
	if (!address.is_empty()) {
		args.push_back("-a");
		args.push_back(address);
	}
	args.push_back(path);
	if (!flags.is_empty()) {
		args.push_back("--");
		for (const String &f : flags) {
			args.push_back(f);
		}
	}
	String output;
	int exit_code = 0;
	err = OS::get_singleton()->execute(link, args, &output, &exit_code, true);
	if (err != OK || exit_code != 0) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Run"), TTR("3dslink couldn't send the game. Is the Homebrew Launcher waiting (press Y) on the same network?") + "\n" + output);
		return ERR_CANT_CONNECT;
	}
	return OK;
}
