/* stb_image implementation for SymphonyStation5's cover art (JPEG and PNG only,
 * decoded from memory). Header vendored in third_party/stb/. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb/stb_image.h"
