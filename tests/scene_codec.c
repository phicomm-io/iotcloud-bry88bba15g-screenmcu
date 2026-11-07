#include "scene.h"
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
  screen_scene scene;
  uint8_t bytes[SCENE_BYTES];
  if (argc == 2 && !strcmp(argv[1], "default")) {
    scene_defaults(&scene);
    if (!scene_encode(&scene, bytes))
      return 1;
    return fwrite(bytes, 1, sizeof(bytes), stdout) == sizeof(bytes) ? 0 : 1;
  }
  if (argc == 2) {
    FILE *f = fopen(argv[1], "rb");
    if (!f)
      return 2;
    size_t n = fread(bytes, 1, sizeof(bytes), f);
    int extra = fgetc(f);
    fclose(f);
    if (n != sizeof(bytes) || extra != EOF || !scene_decode(&scene, bytes, n))
      return 3;
    printf("%lu %lu\n", (unsigned long)scene.revision,
           (unsigned long)scene_crc32(bytes, n));
    return 0;
  }
  return 2;
}
