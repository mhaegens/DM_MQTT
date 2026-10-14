#include "config_store.h"
#include "config_validation.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

bool config_json_valid(const char *json) {
    cJSON *root=cJSON_ParseWithOpts(json,NULL,true);
    bool valid=root&&config_root_valid(root);cJSON_Delete(root);return valid;
}
static void expect_file(const char *path,const char *expected) {
    char data[1024];FILE *f=fopen(path,"r");if(!f)fprintf(stderr,"Missing expected file: %s\n",path);assert(f);
    size_t length=fread(data,1,sizeof(data)-1,f);data[length]=0;fclose(f);
    assert(!strcmp(data,expected));
}
int main(void) {
    assert(!mkdir(TEST_BASE "/sd",0700));assert(!mkdir(TEST_BASE "/flash",0700));
    config_store_init();
    const char *a="{\"plant_name\":\"Profile A\",\"machines\":[],\"tanks\":[],\"people\":[]}";
    const char *b="{\"plant_name\":\"Profile B\",\"machines\":[],\"tanks\":[],\"people\":[]}";
    bool sd=false;char stored[CFG_TPL_NAME_MAX];
    assert(!config_store_import("my_profile.json",a,strlen(a),&sd,stored,sizeof(stored))&&sd);
    assert(!strcmp(stored,"my_profile.json"));
    expect_file(TEST_BASE "/sd/my_profile.json",a);expect_file(TEST_BASE "/flash/my_profile.json",a);
    assert(!config_store_import("my_profile.json",b,strlen(b),&sd,stored,sizeof(stored))&&sd);
    assert(!strcmp(stored,"my_profile_2.json"));
    expect_file(TEST_BASE "/sd/my_profile.json",a);expect_file(TEST_BASE "/sd/my_profile_2.json",b);
    assert(!config_store_import("my_profile.json",b,strlen(b),&sd,stored,sizeof(stored)));
    assert(!strcmp(stored,"my_profile_2.json")); // identical re-upload adds no duplicate
    for(int i=0;i<30;i++){char name[48];snprintf(name,sizeof(name),"profile_%02d.json",i);assert(!config_store_save_template(name,a,strlen(a),NULL));}
    FILE *broken=fopen(TEST_BASE "/sd/needs_attention.json","w");assert(broken);fputs("{broken",broken);fclose(broken);
    int count;ConfigTemplate *all=config_store_list_all(&count);assert(all&&count==33);
    bool found_invalid=false;
    for(int i=0;i<count;i++){
        assert(all[i].from_sd);if(i)assert(strcasecmp(all[i-1].name,all[i].name)<0);
        if(!strcmp(all[i].name,"needs_attention.json")){assert(!all[i].valid);found_invalid=true;}
    }
    assert(found_invalid);free(all);
    ConfigStorageStats stats;config_store_get_stats(&stats);assert(stats.profiles==33&&stats.invalid_profiles==1);
    ConfigTemplate page[CFG_TPL_MAX];assert(config_store_list(page,CFG_TPL_MAX)==12);
    assert(!config_store_save_active(a,strlen(a)));
    char active[1024];bool from_sd=false;assert(config_store_read_active(active,sizeof(active),&from_sd)>0&&from_sd&&!strcmp(active,a));
    assert(config_store_save_active("broken",6));expect_file(TEST_BASE "/sd/sapdm_config.json",a);
    // A failed staging write must preserve the previous SD active file.
    assert(!mkdir(TEST_BASE "/sd/sapdm_config.json.pending",0700));
    assert(config_store_save_active(b,strlen(b)));expect_file(TEST_BASE "/sd/sapdm_config.json",a);
    assert(!rmdir(TEST_BASE "/sd/sapdm_config.json.pending"));
    // Recover the old file if power fails between backup and final rename.
    assert(!rename(TEST_BASE "/sd/sapdm_config.json",TEST_BASE "/sd/sapdm_config.json.bak"));
    assert(config_store_read_active(active,sizeof(active),&from_sd)>0&&from_sd&&!strcmp(active,a));
    expect_file(TEST_BASE "/sd/sapdm_config.json",a);
    assert(config_store_activate("needs_attention.json",true));
    assert(!config_store_activate("my_profile_2.json",true));
    expect_file(TEST_BASE "/sd/sapdm_config.json",b);expect_file(TEST_BASE "/flash/sapdm_config.json",b);
    char safe[48];config_store_sanitize_tpl_name("../../my:profile.json",safe,sizeof(safe));assert(!strcmp(safe,"my_profile.json"));
    // Recover older unnamed portal uploads from either active store, even
    // if the card and flash disagree. Repeating boot seeding is idempotent.
    const char *legacy_sd="{\"plant_name\":\"Legacy SD\",\"machines\":[],\"tanks\":[],\"people\":[]}";
    const char *legacy_flash="{\"plant_name\":\"Legacy flash\",\"machines\":[],\"tanks\":[],\"people\":[]}";
    FILE *legacy=fopen(TEST_BASE "/sd/sapdm_config.json","w");assert(legacy);fputs(legacy_sd,legacy);fclose(legacy);
    legacy=fopen(TEST_BASE "/flash/sapdm_config.json","w");assert(legacy);fputs(legacy_flash,legacy);fclose(legacy);
    assert(config_store_seed_sd()>=0);
    expect_file(TEST_BASE "/sd/saved_active_config.json",legacy_sd);
    expect_file(TEST_BASE "/sd/saved_active_config_2.json",legacy_flash);
    int before=0;ConfigTemplate *after=config_store_list_all(&before);free(after);
    assert(config_store_seed_sd()>=0);
    int after_count=0;after=config_store_list_all(&after_count);free(after);assert(after_count==before);
    // A flash-only save after successful SD synchronization must re-enable
    // flash enumeration so a failed card write cannot hide the saved profile.
    assert(!mkdir(TEST_BASE "/sd/flash_fallback.json.pending",0700));
    assert(!config_store_import("flash_fallback.json",a,strlen(a),&sd,stored,sizeof(stored))&&!sd);
    bool found=false;after=config_store_list_all(&after_count);
    for(int i=0;i<after_count;i++)if(!strcmp(after[i].name,"flash_fallback.json"))found=!after[i].from_sd;
    free(after);assert(found);
    puts("Profile storage: distinct uploads retained, identical re-uploads deduplicated, SD/flash copies verified, 33-file catalog without truncation, invalid files visible, staged-write failure preservation and interrupted-write recovery passed");
}
