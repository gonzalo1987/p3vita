// Implementación de stb_image limitada a los formatos que usa el juego.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"
