#include "config_validation.h"
#include <math.h>
#include <string.h>
#include <ctype.h>

static bool text(const cJSON *j, const char *key, size_t max, bool required)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    if (!v) return !required;
    return cJSON_IsString(v) && strlen(v->valuestring) <= max && (!required || v->valuestring[0]);
}
static bool token(const cJSON *j, const char *key, size_t max)
{
    if (!text(j, key, max, true)) return false;
    const unsigned char *s = (const unsigned char *)cJSON_GetObjectItemCaseSensitive(j, key)->valuestring;
    for (; *s; s++) if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
                         (*s >= '0' && *s <= '9') || *s == '_' || *s == '-')) return false;
    return true;
}
static bool number(const cJSON *j, const char *key, double *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || fabs(v->valuedouble) > 1e9) return false;
    *out = v->valuedouble; return true;
}
static bool unique(const cJSON *array, const cJSON *item, const char *key)
{
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, key);
    const cJSON *other;
    cJSON_ArrayForEach(other, array) {
        if (other == item) return true;
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(other, key);
        if (cJSON_IsString(value) && !strcmp(value->valuestring, id->valuestring)) return false;
    }
    return true;
}
static bool choice(const cJSON *j, const char *key, const char *values[]) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j,key);
    if (!v) return true;
    if (!cJSON_IsString(v)) return false;
    for (int i = 0; values[i]; i++) if (!strcmp(v->valuestring,values[i])) return true;
    return false;
}
static bool color(const cJSON *j, const char *key, bool required) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j,key);
    if (!v) return !required;
    if (!cJSON_IsString(v) || strlen(v->valuestring) != 8 || strncmp(v->valuestring,"0x",2)) return false;
    for (int i = 2; i < 8; i++) if (!isxdigit((unsigned char)v->valuestring[i])) return false;
    return true;
}
static bool boolean(const cJSON *j, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j,key);
    return !v || cJSON_IsBool(v);
}
static bool metric_valid(const cJSON *m, bool level)
{
    double lo, hi, initial, step;
    if (!cJSON_IsObject(m) || !token(m,"key",31) || !text(m,"label",47,false) || !text(m,"unit",15,false) ||
        !number(m,"min_val",&lo) || !number(m,"max_val",&hi) || !number(m,"init_val",&initial) ||
        !number(m,"step",&step) || !(lo < hi) || !(step > 0) || initial < lo || initial > hi) return false;
    const char *viz_types[] = {"spark","arc","thermo","ph","tank","bullet",NULL};
    const char *inputs[] = {"step","slider",NULL};
    if (!choice(m,"viz",viz_types) || !choice(m,"input",inputs) || !color(m,"color",false)) return false;
    const cJSON *viz = cJSON_GetObjectItemCaseSensitive(m,"viz");
    if (level && (!cJSON_IsString(viz) || strcmp(viz->valuestring,"tank") || lo != 0 || hi != 100)) return false;
    const cJSON *thresholds = cJSON_GetObjectItemCaseSensitive(m,"thresholds");
    if (thresholds && (!cJSON_IsArray(thresholds) || cJSON_GetArraySize(thresholds) > 3)) return false;
    const cJSON *thr; double previous = -INFINITY;
    cJSON_ArrayForEach(thr,thresholds) {
        double limit;
        if (!cJSON_IsObject(thr) || !number(thr,"limit",&limit) || limit < previous || !color(thr,"color",true)) return false;
        previous = limit;
    }
    const cJSON *sim = cJSON_GetObjectItemCaseSensitive(m,"sim");
    if (sim && !cJSON_IsObject(sim)) return false;
    const char *profiles[] = {"random_walk","sine","ramp","steady",NULL};
    if (!choice(sim,"profile",profiles)) return false;
    const char *parameters[] = {"rand_min","rand_max","amplitude","period_s","offset"};
    for (int i = 0; i < 5; i++) {
        double value;
        if (cJSON_GetObjectItemCaseSensitive(sim,parameters[i]) && !number(sim,parameters[i],&value)) return false;
    }
    const cJSON *period = cJSON_GetObjectItemCaseSensitive(sim,"period_s");
    if (period && (!cJSON_IsNumber(period) || !isfinite(period->valuedouble) || period->valuedouble <= 0)) return false;
    return true;
}
bool config_root_valid(const cJSON *root)
{
    if (!cJSON_IsObject(root) || !text(root,"plant_name",39,false)) return false;
    const char *names[] = {"machines","tanks","people"};
    const int limits[] = {10,3,12};
    bool has_array = false;
    for (int kind = 0; kind < 3; kind++) {
        const cJSON *list = cJSON_GetObjectItemCaseSensitive(root,names[kind]);
        if (!list) continue;
        if (!cJSON_IsArray(list) || cJSON_GetArraySize(list) > limits[kind]) return false;
        has_array = true;
        const cJSON *item;
        cJSON_ArrayForEach(item,list) {
            if (!cJSON_IsObject(item) || !text(item,"id",15,true) || !unique(list,item,"id") ||
                !text(item,"name",kind == 2 ? 47 : 31,false)) return false;
            if (kind == 2) {
                if (!color(item,"avatar_color",false) || !text(item,"uid",23,false) || !text(item,"role",47,false) || !text(item,"role_key",15,false) ||
                    !text(item,"dept",31,false) || !text(item,"plant",31,false)) return false;
                const cJSON *certs = cJSON_GetObjectItemCaseSensitive(item,"certs");
                if (certs && (!cJSON_IsArray(certs) || cJSON_GetArraySize(certs) > 6)) return false;
                const cJSON *cert;
                cJSON_ArrayForEach(cert,certs) if (!cJSON_IsString(cert) || strlen(cert->valuestring) > 31) return false;
                continue;
            }
            if (!token(item,"id",15) || !text(item,"type",47,false)) return false;
            if (!boolean(item,"running") || !boolean(item,"auto_pulse")) return false;
            const cJSON *pulses = cJSON_GetObjectItemCaseSensitive(item,"pulses");
            if (pulses && (!cJSON_IsNumber(pulses) || !isfinite(pulses->valuedouble) || pulses->valuedouble < 0 ||
                           pulses->valuedouble > 2147483647.0 || floor(pulses->valuedouble) != pulses->valuedouble)) return false;
            const cJSON *metrics = cJSON_GetObjectItemCaseSensitive(item,"metrics");
            if (!cJSON_IsArray(metrics) || cJSON_GetArraySize(metrics) > 8 || (kind == 1 && !cJSON_GetArraySize(metrics))) return false;
            const cJSON *m; int k = 0;
            cJSON_ArrayForEach(m,metrics) {
                if (!metric_valid(m,kind == 1 && k == 0) || !unique(metrics,m,"key")) return false;
                k++;
            }
        }
    }
    return has_array;
}
