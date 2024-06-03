import sys
import os
import platform

def run(cmd):
    print("Running: %s" % cmd)
    res = os.system(cmd)
    code = 0
    if (os.name == 'nt'):
        code = res
    else:
        code = os.WEXITSTATUS(res)
    if code != 0:
        print("Error: return code: " + str(code))
        sys.exit(code)

def build_luajit(env, extension=False):
    cpu_count=os.cpu_count()
    if extension or not env.msvc:
        os.chdir("luaJIT")
        
        # cross compile posix->windows
        if (os.name == 'posix') and env['platform'] == 'windows':
            host_arch = platform.machine()
            run("make clean")
            if (host_arch != env['arch']):
                if (host_arch == 'x86_64' and env['arch'] == 'x86_32'):
                    host_cc = env['luaapi_host_cc'] + ' -m32'
                    run('make HOST_CC="%s" CROSS="%s" BUILDMODE="static" TARGET_SYS=Windows -j%d' % (host_cc, env['CC'].replace("-gcc", "-").replace("-clang", "-"), cpu_count))

                else:
                    print("ERROR: Unsupported cross compile!")
                    sys.exit(-1)
            else:
                run('make HOST_CC="%s" CROSS="%s" BUILDMODE="static" TARGET_SYS=Windows -j%d' % (env['luaapi_host_cc'], env['CC'].replace("-gcc", "-").replace("-clang", "-"), cpu_count))

        elif env['platform']=='macos':
            run("make clean MACOSX_DEPLOYMENT_TARGET=10.12")
            arch = env['arch']
            if arch == "universal":
                run('make CC="%s" TARGET_FLAGS="-arch x86_64" MACOSX_DEPLOYMENT_TARGET=10.12 -j%d' % (env['CC'], cpu_count))
                run('mv src/libluajit.a src/libluajit64.a')
                run('make clean MACOSX_DEPLOYMENT_TARGET=10.12')
                run('make CC="%s" TARGET_FLAGS="-arch arm64" MACOSX_DEPLOYMENT_TARGET=10.12 -j%d' % (env['CC'], cpu_count))
                run('lipo -create src/libluajit.a src/libluajit64.a -output src/libluajit.a')
                run('rm src/libluajit64.a')
            else:
                run('make CC="%s" TARGET_FLAGS="-arch %s" MACOSX_DEPLOYMENT_TARGET=10.12 -j%d' % (env['CC'], arch, cpu_count))
        elif env['platform']=='linuxbsd' or env['platform']=='linux':
            host_arch = platform.machine()
            run("make clean")
            if (host_arch != env['arch']):
                if (host_arch == 'x86_64' and env['arch'] == 'x86_32'):
                    host_cc = env['luaapi_host_cc'] + ' -m32'
                    run('make HOST_CC="%s" CROSS="%s" BUILDMODE="static" -j%d' % (host_cc, env['CC'].replace("-gcc", "-").replace("-clang", "-"), cpu_count))

                else:
                    print("ERROR: Unsupported cross compile!")
                    sys.exit(-1)

            else:
                run('make CC="%s" BUILDMODE="static" -j%d' % (env['CC'], cpu_count))
        elif env['platform']=='android' and env['arch']=="arm64":
            run("make clean")
            # Android/ARM64, aarch64, Android 5.0+ (L)
            NDKDIR=env["ANDROID_NDK_ROOT"]
            NDKBIN=f"{NDKDIR}/toolchains/llvm/prebuilt/linux-x86_64/bin"
            NDKCC=env["CC"]
            r=f"""make CC={NDKCC} TARGET_LD={NDKCC} TARGET_STRIP={NDKBIN}/llvm-strip BUILDMODE="static" -j{cpu_count}"""
            run(r)
        else:
            print("ERROR: Unsupported platform '%s'." % env['platform'])
            sys.exit(-1)
    else:
        os.chdir("luaJIT/src")
        run("msvcbuild static")
