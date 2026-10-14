#include "config_validation.h"
#include "default_config.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    cJSON *root = cJSON_Parse(DEFAULT_CONFIG_JSON); assert(root && config_root_valid(root)); cJSON_Delete(root);
    for (int i=1;i<argc;i++) {
        FILE *f = fopen(argv[i],"rb"); assert(f); fseek(f,0,SEEK_END); long n=ftell(f); rewind(f);
        char *json=calloc((size_t)n+1,1); assert(json); assert(fread(json,1,(size_t)n,f)==(size_t)n); fclose(f);
        root=cJSON_Parse(json); assert(root && config_root_valid(root)); free(json); cJSON_Delete(root);
    }
    root=cJSON_Parse(DEFAULT_CONFIG_JSON);
    cJSON *machine=cJSON_GetArrayItem(cJSON_GetObjectItem(root,"machines"),0);
    cJSON *metric=cJSON_GetArrayItem(cJSON_GetObjectItem(machine,"metrics"),0);
    cJSON_SetNumberValue(cJSON_GetObjectItem(metric,"step"),0);
    assert(!config_root_valid(root)); cJSON_Delete(root);
    root=cJSON_Parse("{\"machines\":{},\"tanks\":[]}"); assert(!config_root_valid(root)); cJSON_Delete(root);
    root=cJSON_Parse("{\"tanks\":[{\"id\":\"raw\",\"metrics\":[]}]}"); assert(!config_root_valid(root)); cJSON_Delete(root);
    puts("Firmware config validation: embedded/default profiles and malformed metric/array rejection passed");
}
