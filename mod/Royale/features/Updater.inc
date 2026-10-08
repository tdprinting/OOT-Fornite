// ---- in-game updater (Android) --------------------------------------------------------------------------------------------------
// "Game updates" on the Battle Royale page: asks GitHub for the newest published build (RoyaleUpdater.java, patches/0018), downloads it
// and opens Android's installer. The game build workflow publishes every main-branch build as a release tagged "build-<number>", and our
// version is "0.<number>", so the numbers compare directly.
#ifdef __ANDROID__
enum UpdaterState { kUpdIdle = 0, kUpdChecking, kUpdChecked, kUpdDownloading, kUpdNeedPermission, kUpdInstalling, kUpdError };

// The build number in ROYALE_BUILD_VERSION ("0.123" -> 123); 0 for a developer build ("dev").
int OwnBuildNumber() {
    const char* dot = std::strchr(ROYALE_BUILD_VERSION, '.');
    return dot != nullptr ? std::atoi(dot + 1) : 0;
}

// RoyaleUpdater, found through the activity's class loader (FindClass on the game thread only sees Android's own classes).
jclass UpdaterClass(JNIEnv* env) {
    static jclass cls = nullptr;
    if (cls != nullptr) return cls;
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (activity == nullptr) return nullptr;
    jclass activityCls = env->GetObjectClass(activity);
    jobject loader = env->CallObjectMethod(activity, env->GetMethodID(activityCls, "getClassLoader", "()Ljava/lang/ClassLoader;"));
    jclass loaderCls = env->FindClass("java/lang/ClassLoader");
    jstring name = env->NewStringUTF("com.dishii.soh.RoyaleUpdater");
    jobject found = env->CallObjectMethod(loader, env->GetMethodID(loaderCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;"), name);
    if (env->ExceptionCheck()) { env->ExceptionClear(); found = nullptr; }
    if (found != nullptr) cls = static_cast<jclass>(env->NewGlobalRef(found));
    env->DeleteLocalRef(name);
    env->DeleteLocalRef(loaderCls);
    env->DeleteLocalRef(loader);
    env->DeleteLocalRef(activityCls);
    env->DeleteLocalRef(activity);
    if (found != nullptr) env->DeleteLocalRef(found);
    return cls;
}

jint UpdaterInt(const char* method) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jclass cls = env != nullptr ? UpdaterClass(env) : nullptr;
    if (cls == nullptr) return -1;
    const jint v = env->CallStaticIntMethod(cls, env->GetStaticMethodID(cls, method, "()I"));
    if (env->ExceptionCheck()) { env->ExceptionClear(); return -1; }
    return v;
}

jlong UpdaterLong(const char* method) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jclass cls = env != nullptr ? UpdaterClass(env) : nullptr;
    if (cls == nullptr) return 0;
    const jlong v = env->CallStaticLongMethod(cls, env->GetStaticMethodID(cls, method, "()J"));
    if (env->ExceptionCheck()) { env->ExceptionClear(); return 0; }
    return v;
}

std::string UpdaterString(const char* method) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jclass cls = env != nullptr ? UpdaterClass(env) : nullptr;
    if (cls == nullptr) return "";
    jstring s = static_cast<jstring>(env->CallStaticObjectMethod(cls, env->GetStaticMethodID(cls, method, "()Ljava/lang/String;")));
    if (env->ExceptionCheck()) { env->ExceptionClear(); return ""; }
    if (s == nullptr) return "";
    const char* chars = env->GetStringUTFChars(s, nullptr);
    std::string out = chars != nullptr ? chars : "";
    if (chars != nullptr) env->ReleaseStringUTFChars(s, chars);
    env->DeleteLocalRef(s);
    return out;
}

// check / download / install, each taking the activity as its Context
void UpdaterCall(const char* method) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jclass cls = env != nullptr ? UpdaterClass(env) : nullptr;
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (cls == nullptr || activity == nullptr) return;
    env->CallStaticVoidMethod(cls, env->GetStaticMethodID(cls, method, "(Landroid/content/Context;)V"), activity);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(activity);
}

#endif // __ANDROID__

