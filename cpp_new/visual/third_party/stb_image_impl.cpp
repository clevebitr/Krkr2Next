// stb_image 的唯一实现单元。
//
// 只编 PNG / JPEG / BMP：KiriKiri 素材里这三种足够（TLG 另走自写解码器），
// 关掉 HDR/PSD/GIF 等可以显著减小目标文件体积与告警面。
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#include "visual/third_party/stb_image.h"
