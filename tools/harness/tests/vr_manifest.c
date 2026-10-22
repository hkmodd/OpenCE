/* an OpenXR runtime's manifest read as port/linux/src/vr_openxr.c reads it: its name and its library's path, from
the manifests Meta's PC runtime and SteamVR install for 32-bit programs */
#include "harness.h"
#include <string.h>

#include "under_test.inc"

static char const oculus[] =
    "{\n"
    "    \"file_format_version\": \"1.0.0\",\n"
    "    \"runtime\":\n"
    "    {\n"
    "        \"name\": \"Oculus OpenXR\",\n"
    "        \"library_path\": \".\\\\LibOVRRTImpl32_1.dll\"\n"
    "    }\n"
    "}\n";

static char const steamvr[] =
    "{\n"
    "\t\"file_format_version\" : \"1.0.0\",\n"
    "\t\"runtime\" : \n"
    "\t{\n"
    "\t\t\"library_path\" : \"bin\\\\vrclient.dll\",\n"
    "\t\t\"name\": \"SteamVR\",\n"
    "\t\t\"VALVE_runtime_is_steamvr\" : true\n"
    "\t}\n"
    "}\n";

int main(int argc, char **argv)
{
    char const *case_name = argc > 1 ? argv[1] : "";
    char text[256], path[256];

    CASE("manifest") {
        CHECK(vr_manifest_string(oculus, "name", text, sizeof(text)) && !strcmp(text, "Oculus OpenXR"), "Meta's name");
        CHECK(vr_manifest_string(oculus, "library_path", text, sizeof(text)) && !strcmp(text, ".\\LibOVRRTImpl32_1.dll"),
            "an escaped backslash is one");
        CHECK(vr_manifest_library_path("C:\\Program Files\\Oculus\\Support\\oculus-runtime\\oculus_openxr_32.json",
            text, path, sizeof(path)) &&
            !strcmp(path, "C:\\Program Files\\Oculus\\Support\\oculus-runtime\\.\\LibOVRRTImpl32_1.dll"),
            "a relative library is in the manifest's folder");
        CHECK(vr_manifest_string(steamvr, "library_path", text, sizeof(text)) && !strcmp(text, "bin\\vrclient.dll"),
            "spaces around the colon");
        CHECK(vr_manifest_string(steamvr, "name", text, sizeof(text)) && !strcmp(text, "SteamVR"),
            "a key after others, not a key that ends with it");
        CHECK(vr_manifest_library_path("/home/a/.steam/steamxr_linux32.json", "bin/vrclient.so", path, sizeof(path)) &&
            !strcmp(path, "/home/a/.steam/bin/vrclient.so"), "a POSIX folder");
        CHECK(vr_manifest_library_path("/etc/xdg/openxr/1/active_runtime.json", "libopenxr_monado.so", path,
            sizeof(path)) && !strcmp(path, "libopenxr_monado.so"), "a bare name is the system's to find");
        CHECK(vr_manifest_library_path("C:\\x\\m.json", "D:\\r\\rt.dll", path, sizeof(path)) &&
            !strcmp(path, "D:\\r\\rt.dll"), "an absolute path is kept");
        CHECK(vr_manifest_string("{\"name\": \"caf\\u00e9 \\u20ac\"}", "name", text, sizeof(text)) &&
            !strcmp(text, "caf\xc3\xa9 \xe2\x82\xac"), "\\u escapes in UTF-8");
        CHECK(!vr_manifest_string("{\"name\": \"unterminated", "name", text, sizeof(text)), "an unended string");
        CHECK(!vr_manifest_string("{\"name\": \"0123456789\"}", "name", text, 8), "a value too long for the buffer");
        CHECK(!vr_manifest_string("{\"name\": \"a\\q\"}", "name", text, sizeof(text)), "an unknown escape");
        CHECK(!vr_manifest_string("{\"name\": \"\\u12\"}", "name", text, sizeof(text)), "a short \\u escape");
        CHECK(!vr_manifest_string("{\"names\": \"x\"}", "name", text, sizeof(text)), "no such key");
        CHECK(vr_manifest_string("{\"library_name\": \"x\", \"name\": \"y\"}", "name", text, sizeof(text)) &&
            !strcmp(text, "y"), "not a key that ends with the one asked for");
        CHECK(!vr_manifest_library_path("C:\\a\\m.json", "b\\0123456789", path, 12), "a path too long for the buffer");
    }
    return 0;
}
