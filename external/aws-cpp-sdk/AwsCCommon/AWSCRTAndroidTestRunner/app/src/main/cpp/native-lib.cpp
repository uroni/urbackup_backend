#include <jni.h>
#include <string>
#include <sstream>

#include <dlfcn.h>

#include <android/log.h>

typedef int(test_fn_t)(int, char**);

extern "C" JNIEXPORT jint JNICALL
Java_software_amazon_awssdk_crt_awscrtandroidtestrunner_NativeTestFixture_runTest(
        JNIEnv *env,
        jobject /* this */,
        jstring jni_name) {
    const char *test_name = env->GetStringUTFChars(jni_name, nullptr);
    __android_log_print(ANDROID_LOG_INFO, "native-test", "RUNNING %s", test_name);

    test_fn_t *test_fn = (test_fn_t*)dlsym(RTLD_DEFAULT, test_name);
    if (!test_fn) {
        __android_log_print(ANDROID_LOG_WARN, "native-test", "%s NOT FOUND", test_name);
        return -1;
    }

    int result = test_fn(0, nullptr);
    __android_log_print(
            result ? ANDROID_LOG_FATAL : ANDROID_LOG_INFO,
            "native-test",
            "%s %s", test_name, result ? "FAILED" : "OK");
    return result;
}
