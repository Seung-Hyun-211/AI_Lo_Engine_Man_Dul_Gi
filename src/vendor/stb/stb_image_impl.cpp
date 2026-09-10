// stb_image, vendored (src/vendor/stb, v2.30, MIT / public-domain).
//
// The single translation unit that defines STB_IMAGE_IMPLEMENTATION - do not
// define it anywhere else. Decoders are limited to the formats the engine
// actually loads; stdio is disabled so stb never touches the filesystem
// (callers hand it a memory buffer). The stb_image types never leave
// src/import/ImageFile.cpp (CLAUDE.md rule 9). Warnings are turned off for this
// file in the vcxproj, same as vendor/ufbx/ufbx.c.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_ONLY_TGA
#include "stb_image.h"
