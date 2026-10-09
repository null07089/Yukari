#include "path_hook.h"

#include <cstring>
#include <string>
#include <vector>

#include "logger.h"
#include "service_match.h"

namespace {
// LineageOS adds this public constant to AssetManager; reflection on it is a
// permissionless ROM fingerprint.
constexpr const char *kHiddenField = "LINEAGE_APK_PATH";

using ListDirFn = jobjectArray (*)(JNIEnv *, jobject, jobject);
using GetFieldByNameFn = jobject (*)(JNIEnv *, jobject, jstring);
using GetFieldsFn = jobjectArray (*)(JNIEnv *, jobject);
using GetFieldsBoolFn = jobjectArray (*)(JNIEnv *, jobject, jboolean);
using ReadlinkStringFn = jstring (*)(JNIEnv *, jobject, jstring);
using ReadlinkBytesFn = jbyteArray (*)(JNIEnv *, jclass, jlong);

ListDirFn g_original_list0 = nullptr;
GetFieldByNameFn g_original_get_declared_field = nullptr;
GetFieldByNameFn g_original_get_public_field = nullptr;
GetFieldsFn g_original_get_declared_fields = nullptr;
GetFieldsBoolFn g_original_get_declared_fields0 = nullptr;
GetFieldsBoolFn g_original_get_declared_fields_unchecked = nullptr;
ReadlinkStringFn g_original_linux_readlink = nullptr;
ReadlinkBytesFn g_original_readlink0 = nullptr;
jmethodID g_field_get_name = nullptr;
bool g_path_hooks_installed = false;

void clear_jni_exception(JNIEnv *env) {
    if (env && env->ExceptionCheck()) env->ExceptionClear();
}

std::string jstring_to_ascii(JNIEnv *env, jstring value) {
    if (!env || !value) return {};
    const char *chars = env->GetStringUTFChars(value, nullptr);
    if (!chars) {
        clear_jni_exception(env);
        return {};
    }
    try {
        std::string out = chars;
        env->ReleaseStringUTFChars(value, chars);
        return out;
    } catch (...) {
        env->ReleaseStringUTFChars(value, chars);
        return {};
    }
}

bool jstring_equals(JNIEnv *env, jstring value, const char *needle) {
    if (!env || !value || !needle) return false;
    return jstring_to_ascii(env, value) == needle;
}

// Only system locations are filtered; application data directories are left
// untouched so target apps keep their own files.
bool is_system_path(const std::string &path) {
    constexpr const char *kRoots[] = {"/system", "/product", "/vendor", "/odm", "/apex",
                                      "/firmware", "/metadata"};
    for (const char *root : kRoots) {
        const size_t length = std::char_traits<char>::length(root);
        if (path.size() >= length && std::memcmp(path.data(), root, length) == 0) return true;
    }
    return false;
}

bool field_is_hidden(JNIEnv *env, jobject field) {
    if (!env || !field || !g_field_get_name) return false;
    auto name = static_cast<jstring>(env->CallObjectMethod(field, g_field_get_name));
    if (env->ExceptionCheck()) {
        clear_jni_exception(env);
        if (name) env->DeleteLocalRef(name);
        return false;
    }
    const bool hidden = name && jstring_equals(env, name, kHiddenField);
    if (name) env->DeleteLocalRef(name);
    return hidden;
}

// Returns the original array or a copy without the hidden field.
jobjectArray filter_field_array(JNIEnv *env, jobjectArray fields) {
    if (!env || !fields || env->ExceptionCheck()) return fields;
    const jsize length = env->GetArrayLength(fields);
    if (env->ExceptionCheck() || length <= 0) {
        clear_jni_exception(env);
        return fields;
    }
    jsize dropped = 0;
    for (jsize index = 0; index < length; ++index) {
        jobject field = env->GetObjectArrayElement(fields, index);
        if (!field) continue;
        if (field_is_hidden(env, field)) ++dropped;
        env->DeleteLocalRef(field);
        if (env->ExceptionCheck()) {
            clear_jni_exception(env);
            return fields;
        }
    }
    if (dropped == 0) return fields;

    jclass field_class = env->FindClass("java/lang/reflect/Field");
    if (!field_class || env->ExceptionCheck()) {
        clear_jni_exception(env);
        return fields;
    }
    jobjectArray filtered = env->NewObjectArray(length - dropped, field_class, nullptr);
    if (!filtered || env->ExceptionCheck()) {
        clear_jni_exception(env);
        return fields;
    }
    jsize write = 0;
    for (jsize index = 0; index < length; ++index) {
        jobject field = env->GetObjectArrayElement(fields, index);
        if (!field) continue;
        if (!field_is_hidden(env, field)) {
            env->SetObjectArrayElement(filtered, write++, field);
            if (env->ExceptionCheck()) {
                clear_jni_exception(env);
                env->DeleteLocalRef(field);
                return fields;
            }
        }
        env->DeleteLocalRef(field);
    }
    return filtered;
}

jobject hook_get_declared_field(JNIEnv *env, jobject clazz, jstring name) {
    if (!g_original_get_declared_field) return nullptr;
    if (jstring_equals(env, name, kHiddenField)) {
        jclass exception = env->FindClass("java/lang/NoSuchFieldException");
        if (exception) env->ThrowNew(exception, kHiddenField);
        return nullptr;
    }
    return g_original_get_declared_field(env, clazz, name);
}

// Class.getField() turns a null result into NoSuchFieldException.
jobject hook_get_public_field(JNIEnv *env, jobject clazz, jstring name) {
    if (!g_original_get_public_field) return nullptr;
    if (jstring_equals(env, name, kHiddenField)) return nullptr;
    return g_original_get_public_field(env, clazz, name);
}

jobjectArray hook_get_declared_fields(JNIEnv *env, jobject clazz) {
    if (!g_original_get_declared_fields) return nullptr;
    return filter_field_array(env, g_original_get_declared_fields(env, clazz));
}

jobjectArray hook_get_declared_fields0(JNIEnv *env, jobject clazz, jboolean public_only) {
    if (!g_original_get_declared_fields0) return nullptr;
    return filter_field_array(env, g_original_get_declared_fields0(env, clazz, public_only));
}

jobjectArray hook_get_declared_fields_unchecked(JNIEnv *env, jobject clazz, jboolean public_only) {
    if (!g_original_get_declared_fields_unchecked) return nullptr;
    return filter_field_array(env,
                              g_original_get_declared_fields_unchecked(env, clazz, public_only));
}

jobjectArray hook_list0(JNIEnv *env, jobject thiz, jobject file) {
    if (!g_original_list0) return nullptr;
    jobjectArray entries = g_original_list0(env, thiz, file);
    if (!entries || env->ExceptionCheck()) return entries;
    const jsize length = env->GetArrayLength(entries);
    if (env->ExceptionCheck() || length <= 0) {
        clear_jni_exception(env);
        return entries;
    }

    std::string path;
    if (file) {
        jclass file_class = env->GetObjectClass(file);
        if (file_class && !env->ExceptionCheck()) {
            jmethodID get_path = env->GetMethodID(file_class, "getPath", "()Ljava/lang/String;");
            if (get_path && !env->ExceptionCheck()) {
                auto value = static_cast<jstring>(env->CallObjectMethod(file, get_path));
                if (!env->ExceptionCheck() && value) path = jstring_to_ascii(env, value);
                if (value) env->DeleteLocalRef(value);
            } else {
                clear_jni_exception(env);
            }
        } else {
            clear_jni_exception(env);
        }
    }
    if (path.empty() || !is_system_path(path)) return entries;

    jsize dropped = 0;
    for (jsize index = 0; index < length; ++index) {
        auto name = static_cast<jstring>(env->GetObjectArrayElement(entries, index));
        if (!name) continue;
        const std::string entry = jstring_to_ascii(env, name);
        if (!entry.empty() && contains_rom_keyword(entry)) ++dropped;
        env->DeleteLocalRef(name);
        if (env->ExceptionCheck()) {
            clear_jni_exception(env);
            return entries;
        }
    }
    if (dropped == 0) return entries;

    jclass string_class = env->FindClass("java/lang/String");
    if (!string_class || env->ExceptionCheck()) {
        clear_jni_exception(env);
        return entries;
    }
    jobjectArray filtered = env->NewObjectArray(length - dropped, string_class, nullptr);
    if (!filtered || env->ExceptionCheck()) {
        clear_jni_exception(env);
        return entries;
    }
    jsize write = 0;
    for (jsize index = 0; index < length; ++index) {
        jobject name = env->GetObjectArrayElement(entries, index);
        if (!name) continue;
        const std::string entry = jstring_to_ascii(env, static_cast<jstring>(name));
        if (entry.empty() || !contains_rom_keyword(entry)) {
            env->SetObjectArrayElement(filtered, write++, name);
            if (env->ExceptionCheck()) {
                clear_jni_exception(env);
                env->DeleteLocalRef(name);
                return entries;
            }
        }
        env->DeleteLocalRef(name);
    }
    log_info("filtered %d ROM-named entr%s from %s", static_cast<int>(dropped),
             dropped == 1 ? "y" : "ies", path.c_str());
    return filtered;
}

// android.system.Os.readlink() routes through libcore.io.Linux.readlink().
// Detection code resolves /proc/self/fd entries with it; scrub ROM keywords
// from the returned target instead of failing the call.
jstring hook_linux_readlink(JNIEnv *env, jobject thiz, jstring path) {
    if (!g_original_linux_readlink) return nullptr;
    jstring result = g_original_linux_readlink(env, thiz, path);
    if (!result || env->ExceptionCheck()) return result;
    std::string value = jstring_to_ascii(env, result);
    if (value.empty() || scrub_rom_keywords(value.data(), value.size()) == 0) return result;
    jstring replacement = env->NewStringUTF(value.c_str());
    if (!replacement || env->ExceptionCheck()) {
        clear_jni_exception(env);
        return result;
    }
    env->DeleteLocalRef(result);
    return replacement;
}

// java.nio.file.Files.readSymbolicLink() routes through
// UnixNativeDispatcher.readlink0(), which returns the raw target bytes.
jbyteArray hook_readlink0(JNIEnv *env, jclass clazz, jlong path_address) {
    if (!g_original_readlink0) return nullptr;
    jbyteArray result = g_original_readlink0(env, clazz, path_address);
    if (!result || env->ExceptionCheck()) return result;
    const jsize length = env->GetArrayLength(result);
    if (env->ExceptionCheck() || length <= 0) {
        clear_jni_exception(env);
        return result;
    }
    try {
        std::vector<jbyte> bytes(static_cast<size_t>(length));
        env->GetByteArrayRegion(result, 0, length, bytes.data());
        if (env->ExceptionCheck()) {
            clear_jni_exception(env);
            return result;
        }
        if (scrub_rom_keywords(reinterpret_cast<char *>(bytes.data()),
                               static_cast<size_t>(length)) == 0) {
            return result;
        }
        jbyteArray replacement = env->NewByteArray(length);
        if (!replacement || env->ExceptionCheck()) {
            clear_jni_exception(env);
            return result;
        }
        env->SetByteArrayRegion(replacement, 0, length, bytes.data());
        if (env->ExceptionCheck()) {
            clear_jni_exception(env);
            env->DeleteLocalRef(replacement);
            return result;
        }
        env->DeleteLocalRef(result);
        return replacement;
    } catch (...) {
        return result;
    }
}
} // namespace

bool install_path_hooks(JNIEnv *env, zygisk::Api *api) {
    if (g_path_hooks_installed) return true;
    if (!env || !api) return false;

    jclass field_class = env->FindClass("java/lang/reflect/Field");
    if (field_class && !env->ExceptionCheck()) {
        g_field_get_name = env->GetMethodID(field_class, "getName", "()Ljava/lang/String;");
    }
    clear_jni_exception(env);

    int installed = 0;

    JNINativeMethod list_methods[] = {
        {"list0", "(Ljava/io/File;)[Ljava/lang/String;",
         reinterpret_cast<void *>(hook_list0)},
    };
    api->hookJniNativeMethods(env, "java/io/UnixFileSystem", list_methods, 1);
    if (list_methods[0].fnPtr && list_methods[0].fnPtr != reinterpret_cast<void *>(hook_list0)) {
        g_original_list0 = reinterpret_cast<ListDirFn>(list_methods[0].fnPtr);
        ++installed;
    }

    JNINativeMethod field_methods[] = {
        {"getDeclaredField", "(Ljava/lang/String;)Ljava/lang/reflect/Field;",
         reinterpret_cast<void *>(hook_get_declared_field)},
        {"getPublicFieldRecursive", "(Ljava/lang/String;)Ljava/lang/reflect/Field;",
         reinterpret_cast<void *>(hook_get_public_field)},
        {"getDeclaredFields", "()[Ljava/lang/reflect/Field;",
         reinterpret_cast<void *>(hook_get_declared_fields)},
        {"getDeclaredFields0", "(Z)[Ljava/lang/reflect/Field;",
         reinterpret_cast<void *>(hook_get_declared_fields0)},
        {"getDeclaredFieldsUnchecked", "(Z)[Ljava/lang/reflect/Field;",
         reinterpret_cast<void *>(hook_get_declared_fields_unchecked)},
    };
    api->hookJniNativeMethods(env, "java/lang/Class", field_methods,
                              static_cast<int>(sizeof(field_methods) / sizeof(field_methods[0])));

    if (field_methods[0].fnPtr && field_methods[0].fnPtr !=
                                      reinterpret_cast<void *>(hook_get_declared_field)) {
        g_original_get_declared_field = reinterpret_cast<GetFieldByNameFn>(field_methods[0].fnPtr);
        ++installed;
    }
    if (field_methods[1].fnPtr && field_methods[1].fnPtr !=
                                      reinterpret_cast<void *>(hook_get_public_field)) {
        g_original_get_public_field = reinterpret_cast<GetFieldByNameFn>(field_methods[1].fnPtr);
        ++installed;
    }
    if (field_methods[2].fnPtr && field_methods[2].fnPtr !=
                                      reinterpret_cast<void *>(hook_get_declared_fields)) {
        g_original_get_declared_fields = reinterpret_cast<GetFieldsFn>(field_methods[2].fnPtr);
        ++installed;
    }
    if (field_methods[3].fnPtr && field_methods[3].fnPtr !=
                                      reinterpret_cast<void *>(hook_get_declared_fields0)) {
        g_original_get_declared_fields0 =
            reinterpret_cast<GetFieldsBoolFn>(field_methods[3].fnPtr);
        ++installed;
    }
    if (field_methods[4].fnPtr && field_methods[4].fnPtr !=
                                      reinterpret_cast<void *>(hook_get_declared_fields_unchecked)) {
        g_original_get_declared_fields_unchecked =
            reinterpret_cast<GetFieldsBoolFn>(field_methods[4].fnPtr);
        ++installed;
    }

    JNINativeMethod readlink_methods[] = {
        {"readlink", "(Ljava/lang/String;)Ljava/lang/String;",
         reinterpret_cast<void *>(hook_linux_readlink)},
    };
    api->hookJniNativeMethods(env, "libcore/io/Linux", readlink_methods, 1);
    if (readlink_methods[0].fnPtr && readlink_methods[0].fnPtr !=
                                        reinterpret_cast<void *>(hook_linux_readlink)) {
        g_original_linux_readlink = reinterpret_cast<ReadlinkStringFn>(readlink_methods[0].fnPtr);
        ++installed;
    }

    JNINativeMethod nio_readlink_methods[] = {
        {"readlink0", "(J)[B", reinterpret_cast<void *>(hook_readlink0)},
    };
    api->hookJniNativeMethods(env, "sun/nio/fs/UnixNativeDispatcher", nio_readlink_methods, 1);
    if (nio_readlink_methods[0].fnPtr && nio_readlink_methods[0].fnPtr !=
                                            reinterpret_cast<void *>(hook_readlink0)) {
        g_original_readlink0 = reinterpret_cast<ReadlinkBytesFn>(nio_readlink_methods[0].fnPtr);
        ++installed;
    }

    if (installed == 0) {
        log_error("path hooks unavailable; Lineage file fingerprints stay visible");
        return false;
    }
    g_path_hooks_installed = true;
    log_info("path hooks installed (%d/8); ROM file names, readlink targets and %s hidden",
             installed, kHiddenField);
    return true;
}
