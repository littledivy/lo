#ifdef __MACH__
#include "main_mac.h"
#elif defined(_WIN64)
#include "main_win.h"
#else
#include "main.h"
#endif

#define USE_V8
/* #define USE_QUICKJS */

#ifdef USE_V8

using v8::V8;
using v8::Platform;

/**
 * fill the provided buffer with random bytes
 * 
 * we can just use /dev/urandom here, like v8 already does, or come up
 * with something better. it would be nice if we could do this from the
 * JS side, but that doesn't seem possible right now
 * 
 * @param buffer Write random bytes in here.
 * @param length Write this number of random bytes, no more, no less.
 */
bool EntropySource(unsigned char* buffer, size_t length) {
  return true;
}

#endif

#ifdef USE_QUICKJS
#include "quickjs/quickjs.h"
#endif

int main(int argc, char** argv) {
#ifdef USE_QUICKJS
  if (argc == 1) {
    fprintf(stdout, "%s %s\nquickjs %s\n", RUNTIME, VERSION, 
      JS_GetVersion());
    return 0;
  }

  JSRuntime* rt = JS_NewRuntime();
  JSContext* ctx = JS_NewContext(rt);

  // quickjs expects a null-terminated C string
  char* js_cstr = (char*)malloc(main_js_len + 1);
  memcpy(js_cstr, main_js, main_js_len);
  js_cstr[main_js_len] = '\0';

  JSValue val = JS_Eval(ctx, js_cstr, main_js_len, "<eval>", JS_EVAL_TYPE_MODULE);
  if (JS_IsException(val)) {
    JSValue e = JS_GetException(ctx);
    fprintf(stderr, "Exception: %s\n", JS_ToCString(ctx, e));
    JS_FreeValue(ctx, e);
  }

  // todo: rename
  if (_v8_cleanup) {
    JS_FreeValue(ctx, val);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
  }
#endif
#ifdef USE_V8
  // if we are called with no arguments, just dump the version and exit
  if (argc == 1) {
    fprintf(stdout, "%s %s\nv8 %s\n", RUNTIME, VERSION, v8::V8::GetVersion());
    return 0;
  }
  // record the start time - this will be made available to JS so we can 
  // measure time to bootstrap the runtime
  uint64_t starttime = lo::hrtime();

  // turn off buffering of stdout and stderr - this is required by V8
  // https://en.cppreference.com/w/c/io/setvbuf
  setvbuf(stdout, nullptr, _IONBF, 0);
  setvbuf(stderr, nullptr, _IONBF, 0);
  // create the v8 platform
  std::unique_ptr<Platform> platform = 
    v8::platform::NewDefaultPlatform(_v8_threads, 
      v8::platform::IdleTaskSupport::kDisabled, 
      v8::platform::InProcessStackDumping::kDisabled, nullptr);
  V8::InitializePlatform(platform.get());
  // set the v8 flags from the internally defined ones
  V8::SetFlagsFromString(v8flags);
  // then any flags specified on command line will override these, if we 
  // allow this
  if (_v8flags_from_commandline == 1) {
    V8::SetFlagsFromCommandLine(&argc, argv, true);
  }
  // V8 requires an entropy source - by default it opens /dev/urandom multiple
  // times on startup, which we want to avoid. so we need to see if we can
  // find a more efficient way of providing entropy at startup
  V8::SetEntropySource(EntropySource);
  V8::Initialize();
  V8::InitializeICU();
  // register any builtins and modules that have been generated in main.h 
  register_builtins();
  // create a new isolate on the main thread. this will block until the 
  // isolate exits
  lo::CreateIsolate(argc, argv, main_js, main_js_len, starttime, 
    RUNTIME, _v8_cleanup, _on_exit, nullptr);
  // if we have the cleanup flag set, clean up memory left behind when isolate
  // exits. this flag should be set if you want to spawn multiple isolates
  // in the same process without memory leaks.
  if (_v8_cleanup) {
    V8::Dispose();
    platform.reset();
  }
#endif
  return 0;
}
