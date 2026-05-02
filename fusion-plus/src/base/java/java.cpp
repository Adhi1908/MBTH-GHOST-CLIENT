#include "java.h"

#include "util/logger.h"
#include "sdk/java/lang/String.h"

JavaVM* vm;
jobject classLoader;
jmethodID mid_findClass;

static void setupClassLoader()
{
    jint classCount = 0;
    jclass* classes = nullptr;
    Java::tiEnv->GetLoadedClasses(&classCount, &classes);

    for (int i = 0; i < classCount; i++)
    {
        char* signature = nullptr;
        Java::tiEnv->GetClassSignature(classes[i], &signature, nullptr);
        
        if (signature)
        {
            if (strcmp(signature, "Lnet/minecraft/client/Minecraft;") == 0)
            {
                jobject classLoaderObj = nullptr;
                Java::tiEnv->GetClassLoader(classes[i], &classLoaderObj);
                
                if (classLoaderObj)
                {
                    classLoader = Java::env->NewGlobalRef(classLoaderObj);
                    Java::env->DeleteLocalRef(classLoaderObj);
                }
                
                Java::tiEnv->Deallocate((unsigned char*)signature);
                break;
            }
            Java::tiEnv->Deallocate((unsigned char*)signature);
        }
    }

    if (classes)
    {
        // Must deallocate the classes array per JVMTI spec
        Java::tiEnv->Deallocate((unsigned char*)classes);
    }
    
    // We still need mid_findClass for later class lookups by the custom FindClass implementation
    jclass c_ClassLoader = Java::env->FindClass("java/lang/ClassLoader");
    if (c_ClassLoader)
    {
        mid_findClass = Java::env->GetMethodID(c_ClassLoader, "findClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        Java::env->DeleteLocalRef(c_ClassLoader);
    }

    Java::initialized = true;
}

void Java::Init()
{
    Java::initialized = false;

    // Check if there is any Java VMs in the injected thread
    jsize count;
    if (JNI_GetCreatedJavaVMs(&vm, 1, &count) != JNI_OK || count == 0)
        return;

    jint res = vm->GetEnv((void**)&Java::env, JNI_VERSION_1_6);
    LOG_INFO("Got Java ENV");

    if (res == JNI_EDETACHED)
        res = vm->AttachCurrentThread((void**)&Java::env, nullptr);
	LOG_INFO("Attached to Java VM");

    if (res != JNI_OK)
        return;

    if (Java::env == nullptr)
        vm->DestroyJavaVM();

    vm->GetEnv((void**)&Java::tiEnv, JVMTI_VERSION);
    setupClassLoader();
	LOG_INFO("Java initialized");

    GetMinecraftVersion();
	LOG_INFO("Got Minecraft version");
}

void Java::Shutdown()
{
    vm->DetachCurrentThread();
}

bool Java::AssignClass(std::string name, jclass &out)
{
    jstring className = Java::env->NewStringUTF(name.c_str());
    jobject findClass = nullptr;
    if (classLoader) {
        findClass = Java::env->CallObjectMethod(classLoader, mid_findClass, className);
        if (Java::env->ExceptionCheck()) Java::env->ExceptionClear();
    }

    if (findClass)
    {
        out = (jclass)findClass;
        return true;
    }

	out = Java::env->FindClass(name.c_str());
    if (Java::env->ExceptionCheck()) Java::env->ExceptionClear();
	if (out) return true;
        
    return false;
}

jclass Java::GetClass(std::string name)
{
    jstring className = Java::env->NewStringUTF(name.c_str());
    jobject findClass = nullptr;
    if (classLoader) {
        findClass = Java::env->CallObjectMethod(classLoader, mid_findClass, className);
        if (Java::env->ExceptionCheck()) Java::env->ExceptionClear();
    }

	if (findClass)
	{
		return (jclass)findClass;
	}

	jclass out = Java::env->FindClass(name.c_str());
    if (Java::env->ExceptionCheck()) Java::env->ExceptionClear();
    return out;
}

jclass Java::FindClass(JNIEnv* env, jvmtiEnv* tienv, const std::string& path)
{
    jint class_count = 0;
    jclass* classes = nullptr;
    jclass foundclass = nullptr;
    tienv->GetLoadedClasses(&class_count, &classes);
    for (int i = 0; i < class_count; ++i)
    {
        char* signature_buffer = nullptr;
        tienv->GetClassSignature(classes[i], &signature_buffer, nullptr);
        std::string signature = signature_buffer;
        tienv->Deallocate((unsigned char*)signature_buffer);
        signature = signature.substr(1);
        signature.pop_back();
        if (signature == path)
        {
            foundclass = (jclass)env->NewLocalRef(classes[i]);
        }
        env->DeleteLocalRef(classes[i]);
    }
    tienv->Deallocate((unsigned char*)classes);
    return foundclass;
}

std::string Java::GetClazzName(jobject obj)
{
    jclass objClass = Java::env->GetObjectClass(obj);
    jmethodID objMethod = Java::env->GetMethodID(objClass, "getClass", "()Ljava/lang/Class;");
    jobject objClassObj = Java::env->CallObjectMethod(obj, objMethod);
    jmethodID objClassNameMethod = Java::env->GetMethodID(Java::env->GetObjectClass(objClassObj), "getName", "()Ljava/lang/String;");
    jstring objClassName = (jstring)Java::env->CallObjectMethod(objClassObj, objClassNameMethod);
    const char* objClassNameChars = Java::env->GetStringUTFChars(objClassName, NULL);
    std::string objClassNameStr = objClassNameChars;
    Java::env->ReleaseStringUTFChars(objClassName, objClassNameChars);
    return objClassNameStr;
}

static bool checkLunarClient
()
{
    jclass minecraftClass;
    Java::AssignClass("net.minecraft.client.Minecraft", minecraftClass);
    if (!minecraftClass) return false;

    jmethodID getMinecraftMethod = Java::env->GetStaticMethodID(minecraftClass, "getMinecraft", "()Lnet/minecraft/client/Minecraft;");
    if (Java::env->ExceptionCheck()) Java::env->ExceptionClear();
    if (!getMinecraftMethod) return false;

    jobject theMinecraft = Java::env->CallStaticObjectMethod(minecraftClass, getMinecraftMethod);
    if (Java::env->ExceptionCheck()) Java::env->ExceptionClear();
    if (!theMinecraft) return false;

    jfieldID launchedVersionField = Java::env->GetFieldID(minecraftClass, "launchedVersion", "Ljava/lang/String;");
    if (Java::env->ExceptionCheck()) Java::env->ExceptionClear();
    if (!launchedVersionField) return false;

    jobject launchedVersion = Java::env->GetObjectField(theMinecraft, launchedVersionField);
    if (Java::env->ExceptionCheck()) Java::env->ExceptionClear();
    if (!launchedVersion) return false;

    std::string version = String(launchedVersion).ToString();

    if (version == "1.8.9") { Java::version = MinecraftVersion::LUNAR_1_8_9; return true; }
    else if (version == "1.7.10") { Java::version = MinecraftVersion::LUNAR_1_7_10; return true; }

    return false;
}

static bool CheckVanilla189()
{
    jclass minecraftClass;
    Java::AssignClass("ave", minecraftClass);
    if (!minecraftClass) return false;

    jmethodID getMinecraftMethod = Java::env->GetStaticMethodID(minecraftClass, "A", "()Lave;");
    if (Java::env->ExceptionCheck()) Java::env->ExceptionClear();
    if (!getMinecraftMethod) return false;

    jobject theMinecraft = Java::env->CallStaticObjectMethod(minecraftClass, getMinecraftMethod);
    if (Java::env->ExceptionCheck()) Java::env->ExceptionClear();
    if (!theMinecraft) return false;

    jfieldID launchedVersionField = Java::env->GetFieldID(minecraftClass, "al", "Ljava/lang/String;");
    if (Java::env->ExceptionCheck()) Java::env->ExceptionClear();
    if (!launchedVersionField) return false;

    jobject launchedVersion = Java::env->GetObjectField(theMinecraft, launchedVersionField);
    if (Java::env->ExceptionCheck()) Java::env->ExceptionClear();
    if (!launchedVersion) return false;

    std::string version = String(launchedVersion).ToString();

    if (version == "1.8.9") { Java::version = MinecraftVersion::VANILLA_1_8_9; return true; }

    return false;
}

static bool checkVanilla1710()
{
    jclass minecraftClass;
    Java::AssignClass("bao", minecraftClass);
    if (!minecraftClass) return false;

    jmethodID getMinecraftMethod = Java::env->GetStaticMethodID(minecraftClass, "B", "()Lbao;");
    if (Java::env->ExceptionCheck()) Java::env->ExceptionClear();
    if (!getMinecraftMethod) return false;

    jobject theMinecraft = Java::env->CallStaticObjectMethod(minecraftClass, getMinecraftMethod);
    if (Java::env->ExceptionCheck()) Java::env->ExceptionClear();
    if (!theMinecraft) return false;

    jfieldID launchedVersionField = Java::env->GetFieldID(minecraftClass, "Z", "Ljava/lang/String;");
    if (Java::env->ExceptionCheck()) Java::env->ExceptionClear();
    if (!launchedVersionField) return false;

    jobject launchedVersion = Java::env->GetObjectField(theMinecraft, launchedVersionField);
    if (Java::env->ExceptionCheck()) Java::env->ExceptionClear();
    if (!launchedVersion) return false;

    std::string version = String(launchedVersion).ToString();

    if (version == "1.7.10") { Java::version = MinecraftVersion::VANILLA_1_7_10; return true; }

    return false;
}

static bool checkForge189()
{
    jclass minecraftClass;
    Java::AssignClass("net.minecraft.client.Minecraft", minecraftClass);
    if (!minecraftClass) return false;

    jmethodID getMinecraftMethod = Java::env->GetStaticMethodID(minecraftClass, "func_71410_x", "()Lnet/minecraft/client/Minecraft;");
    if (Java::env->ExceptionCheck()) Java::env->ExceptionClear();
    if (!getMinecraftMethod) return false;

    jobject theMinecraft = Java::env->CallStaticObjectMethod(minecraftClass, getMinecraftMethod);
    if (Java::env->ExceptionCheck()) Java::env->ExceptionClear();
    if (!theMinecraft) return false;

    jfieldID launchedVersionField = Java::env->GetFieldID(minecraftClass, "field_110447_Z", "Ljava/lang/String;");
    if (Java::env->ExceptionCheck()) Java::env->ExceptionClear();
    if (!launchedVersionField) return false;

    jobject launchedVersion = Java::env->GetObjectField(theMinecraft, launchedVersionField);
    if (Java::env->ExceptionCheck()) Java::env->ExceptionClear();
    if (!launchedVersion) return false;

    std::string version = String(launchedVersion).ToString();

	if (version.find("1.8.9") != std::string::npos) { Java::version = MinecraftVersion::FORGE_1_8_9; return true; }

    return false;
}

static bool checkForge1710()
{
    jclass minecraftClass;
    Java::AssignClass("net.minecraft.client.Minecraft", minecraftClass);
    if (!minecraftClass) return false;

    jmethodID getMinecraftMethod = Java::env->GetStaticMethodID(minecraftClass, "func_71410_x", "()Lnet/minecraft/client/Minecraft;");
    if (Java::env->ExceptionCheck()) Java::env->ExceptionClear();
    if (!getMinecraftMethod) return false;

    jobject theMinecraft = Java::env->CallStaticObjectMethod(minecraftClass, getMinecraftMethod);
    if (Java::env->ExceptionCheck()) Java::env->ExceptionClear();
    if (!theMinecraft) return false;

    jfieldID launchedVersionField = Java::env->GetFieldID(minecraftClass, "field_110447_Z", "Ljava/lang/String;");
    if (Java::env->ExceptionCheck()) Java::env->ExceptionClear();
    if (!launchedVersionField) return false;

    jobject launchedVersion = Java::env->GetObjectField(theMinecraft, launchedVersionField);
    if (Java::env->ExceptionCheck()) Java::env->ExceptionClear();
    if (!launchedVersion) return false;

    std::string version = String(launchedVersion).ToString();
	LOG_INFO("Version: %s", version.c_str());

	if (version.find("1.7.10") != std::string::npos) { Java::version = MinecraftVersion::FORGE_1_7_10; return true; }

    return false;
}


void Java::GetMinecraftVersion()
{
    if (checkLunarClient()) return;
    if (CheckVanilla189()) return;
	if (checkForge189()) return;
	if (checkVanilla1710()) return;
	if (checkForge1710()) return;
    
    Java::version = MinecraftVersion::UNKNOWN;
}

template<>
std::string Java::Convert<std::string>(jobject obj)
{
    const char* str = Java::env->GetStringUTFChars((jstring)obj, nullptr);
    std::string result(str);
    Java::env->ReleaseStringUTFChars((jstring)obj, str);
    return result;
}

template<>
int Java::Convert<int>(jobject obj)
{
    jclass cls = Java::env->GetObjectClass(obj);
    jmethodID mid = Java::env->GetMethodID(cls, "intValue", "()I");
    Java::env->DeleteLocalRef(cls);
    return Java::env->CallIntMethod(obj, mid);
}

template<>
float Java::Convert<float>(jobject obj)
{
    jclass cls = Java::env->GetObjectClass(obj);
    jmethodID mid = Java::env->GetMethodID(cls, "floatValue", "()F");
    Java::env->DeleteLocalRef(cls);
    return Java::env->CallFloatMethod(obj, mid);
}