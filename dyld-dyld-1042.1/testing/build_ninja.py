#!/usr/bin/python3

import plistlib
import string
import argparse
import sys
import os
import tempfile
import shutil
import subprocess
import re
import hashlib
import textwrap
from string import Template

class BufferedFile:
    def __init__(self, fileName):
        self.data = ""
        self.fileName = os.path.abspath(fileName)
    def __enter__(self):
        return self
    def __exit__(self, type, value, traceback):
        if os.path.exists(self.fileName):
            with open(self.fileName, "r") as file:
                fileData = file.read()
                if fileData == self.data: return
        else:
            dir = os.path.dirname(self.fileName)
            if not os.path.exists(dir):
                os.makedirs(dir)
        with open(self.fileName, "w") as file:
            file.write(self.data)
    def write(self, strn):
        self.data += str(strn)

class NinjaFile:
    class Variable:
        def __init__(self, name, value):
            self.name = name
            self.value = value
        def __lt__(self, other):
            return self.name.__lt__(other.name)
        def __str__(self):
            return NinjaFile.lineWrap("{} = {}".format(self.name, self.value))
    
    class Rule:
        def __init__(self, name, command, depfile):
            self.name = name
            self.command = command
            self.depfile = depfile
        def __lt__(self, other):
            return self.name.__lt__(other.name)
        def __str__(self):
            result = NinjaFile.lineWrap("rule {}".format(self.name))
            if self.command: result += ("\n"+ NinjaFile.lineWrap("    command = {}".format(self.command)))
            if self.depfile:
                result += ("\n" + NinjaFile.lineWrap("    deps = gcc"))
                result += ("\n" + NinjaFile.lineWrap("    depfile = {}".format(self.depfile)))
            return result
    class Target:
        def __init__(self, rule):
            self.rule = rule
            self.output = ""
            self.inputs = []
            self.variables = []
            self.dependencies = []
        def __lt__(self, other):
            return self.output.__lt__(other.output)
        def __str__(self):
            self.inputs.sort()
            self.variables.sort()
            self.dependencies.sort()
            buildLine = "build {}: {}".format(self.output, self.rule)
            if self.inputs: buildLine += " {}".format(" ".join(self.inputs))
            if self.dependencies: buildLine += " | {}".format(" ".join(self.dependencies))
            result = NinjaFile.lineWrap(buildLine)
            for variable in self.variables: result += ("\n" + NinjaFile.lineWrap("    " + str(variable)))
            return result

        def addVariable(self, name, value):
            self.variables.append(NinjaFile.Variable(name, value))

        def addDependency(self, dependency):
            if isinstance(dependency, str):
                self.dependencies.append(dependency)
            elif isinstance(dependency, NinjaFile.Target):
                self.dependencies.append(dependency.output)
            else:
                raise ValueError("dependency must be a string or NinjaFile.Target")
        def addInput(self, input):
            if isinstance(input, str):
                self.inputs.append(input)
            elif isinstance(input, NinjaFile.Target):
                self.inputs.append(input.output)
            else:
                raise ValueError("input must be a string or NinjaFile.Target")
    class Include:
        def __init__(self, file):
            self.file = file
        def __lt__(self, other):
            return self.file.__lt__(other.file)
        def __str__(self):
            return NinjaFile.lineWrap("include {}".format(self.file))

    def __init__(self, fileName):
        self.fileName = os.path.abspath(fileName)
        self.rules = []
        self.variables = []
        self.targets = []
        self.includes = []
    def __enter__(self):
        return self
    def __exit__(self, type, value, traceback):
        with BufferedFile(self.fileName) as file:
            file.write(str(self))
    def addRule(self, name, command, deps, addEnvVars=True):
        modifiedCmd = command
        if addEnvVars:
            modifiedCmd = '$envvars ' + modifiedCmd
        self.rules.append(NinjaFile.Rule(name, modifiedCmd, deps))
    def addVariable(self, name, value): self.variables.append(NinjaFile.Variable(name, value))
    def addInclude(self, file): self.includes.append(NinjaFile.Include(file))
    def newTarget(self, type, name):
        target = NinjaFile.Target(type)
        target.output = name
        self.targets.append(target)
        return target
    def findTarget(self, name):
        #PERF If this gets to be significant we can sort the array and binary search it
        for target in self.targets:
            if target.output == name: return target
        raise ValueError("Target \"{}\"  not found".format(name))
    def deleteTarget(self, name): self.targets.remove(self.findTarget(name))
    def __str__(self):
        self.variables.sort()
        self.rules.sort()
        self.targets.sort()
        self.includes.sort()
        subs = {
            "VARIABLES": "\n".join(map(str, self.variables)),
            "RULES": "\n\n".join(map(str, self.rules)),
            "TARGETS": "\n\n".join(map(str, self.targets)),
            "INCLUDES": "\n\n".join(map(str, self.includes))
        }
        return string.Template(
"""ninja_required_version = 1.6

$INCLUDES

$VARIABLES

$RULES

$TARGETS

""").safe_substitute(subs)
#    wrapper = textwrap.TextWrapper(width = 130, subsequent_indent = "    ", break_long_words = False)
    @classmethod
    def lineWrap(cls, line):
        if len(line) <= 132: return line
        result = ""
        currentIdx = 0
        wrappedLineLeadingSpace = "  "
        firstLineIndent = 0
        if line[0].isspace():
            firstLineIndent = 4
            result = "    "
            wrappedLineLeadingSpace = "      "
        trailer = " $"
        wrappedLineLeadingSpaceLen = len(wrappedLineLeadingSpace)
        lineSpaceAvailable = 132-(firstLineIndent+wrappedLineLeadingSpaceLen)
        words = line.split()
        wordsCount = len(words)-1
        for idx, word in enumerate(words):
            isLastWord = idx == len(words) - 1
            wordLen = len(word)
            if (wordLen <= lineSpaceAvailable and idx == wordsCount):
                result += word
            elif wordLen <= lineSpaceAvailable+2:
                result += word
                if not isLastWord:
                    result += ' '
                lineSpaceAvailable -= (wordLen)
            else:
                result += "$\n{}{}".format(wrappedLineLeadingSpace, word)
                if not isLastWord:
                    result += ' '
                lineSpaceAvailable = 132-(wrappedLineLeadingSpaceLen+wordLen)
        return result

def processBuildLines(ninja, buildLines, testName, platform, osFlag, osVers, forceArchs, testDstDir, testSrcDir):
    testInstallTarget = ninja.newTarget("phony", "install-{}".format(testName))
    testTarget = ninja.newTarget("phony", testName)
    ninja.findTarget("all").addInput(testTarget)
    ninja.findTarget("install").addInput(testInstallTarget)
    for buildLine in buildLines:
        minOS = None
        args = buildLine.split()

        # Handle environment variables.
        # This sets environment variables for several build commands.
        envRegex = re.compile(r'^[a-zA-Z0-9_]+=[a-zA-Z0-9_]+$')
        envVars = []
        for idx, arg in enumerate(args):
            if envRegex.match(arg):
                print("  Adding env var {}".format(arg))
                envVars.append(arg)
                continue
            # Failed match so stop here
            break
        # Re-adjust args to parse the remaining args.
        args = args[idx:]
        def addEnvVarsTo(target):
            if len(envVars) > 0:
                target.addVariable("envvars", " ".join(envVars))

        if args[0] == "$DTRACE":
            target = None
            for idx, arg in enumerate(args):
                if arg == "-o": target = ninja.newTarget("dtrace", args[idx+1])
            for idx, arg in enumerate(args):
                if arg == "-s": target.addInput(testSrcDir + "/" + args[idx+1])
            if target:
                addEnvVarsTo(target)
        elif args[0] == "$CP":
            target = ninja.newTarget("cp", args[2])
            variablesWithAbsPath = {'$asanDylibPath'}
            if args[1] in variablesWithAbsPath:
                target.addInput(args[1])
            else:
                target.addInput(testSrcDir + "/" + args[1])
            testTarget.addInput(target)
            addEnvVarsTo(target)
            installTarget = ninja.newTarget("install", "$INSTALL_DIR/AppleInternal/CoreOS/tests/dyld/{}".format(args[2][9:]))
            installTarget.addInput(target.output)
            installTarget.addVariable("mode", "0644")
            addEnvVarsTo(installTarget)
            testInstallTarget.addInput(installTarget)
        elif args[0] == "$SYMLINK":
            target = ninja.newTarget("symlink", args[2])
            target.addVariable("source", args[1])
            addEnvVarsTo(target)
            testTarget.addInput(target)
            installTarget = ninja.newTarget("symlink", "$INSTALL_DIR/AppleInternal/CoreOS/tests/dyld/{}".format(args[2][9:]))
            installTarget.addVariable("source", args[1])
            addEnvVarsTo(installTarget)
            testInstallTarget.addInput(installTarget)
        elif args[0] == "$STRIP":
            # This is not ideal, but a full argument parser for strip is also a bit heavyweight.
            # So for now, the first argument is the target, and remaining arguments are passed to strip
            target = ninja.findTarget(args[1])
            target.addVariable("extraCmds", "&& strip {} {}".format(target.output, ' '.join(args[2:])))
        elif args[0] == "$INSTALL_NAME_TOOL":
            targetBinary = args[-1]
            target = ninja.findTarget(targetBinary)
            remainingArgs = args[1:-1]
            target.addVariable("extraCmds", "&& install_name_tool {} {}".format(
                target.output,
                ' '.join(remainingArgs))
            )
        elif args[0] == "$SKIP_INSTALL":
            target = "$INSTALL_DIR/AppleInternal/CoreOS/tests/dyld/{}".format(args[1][9:])
            ninja.deleteTarget(target)
            testInstallTarget.inputs.remove(target)
        elif args[0] == "$DYLD_ENV_VARS_ENABLE":
            if platform != "macos":
                target = ninja.findTarget(args[1])
                target.addVariable("entitlements", "--entitlements $SRCROOT/configs/get_task_allow_entitlement.plist")
        elif args[0] == "$TASK_FOR_PID_ENABLE":
            target = ninja.findTarget(args[1])
            target.addVariable("entitlements", "--entitlements $SRCROOT/configs/task_read_for_pid_entitlement.plist")
        elif args[0] == "$DEXT_SPAWN_ENABLE":
            target = ninja.findTarget(args[1])
            target.addVariable("entitlements", "--entitlements $SRCROOT/configs/dext_spawn_entitlement.plist")
        elif args[0] in ["$CC", "$CXX", "$DKCC"]:
            tool = args[0][1:].lower()
            sources = []
            cflags = []
            ldflags = []
            dependencies = []
            skipCount = 0
            linkTarget = None
            linkTestSupport = tool != "dkcc"
            explicitLinkTestSupport = False
            isDylib = False
            platformVersion = None
            targetNames = [target.output for target in ninja.targets]
            args = [escapedArg.replace("\"", "\\\"") for escapedArg in args[1:]]
            #First find the target
            for idx, arg in enumerate(args):
                if arg == "-o":
                    linkTarget = ninja.newTarget("{}-link".format(tool), args[idx+1])
                    addEnvVarsTo(linkTarget)
                    testTarget.addInput(linkTarget);
                    break
            skipCount = 0
            for idx, arg in enumerate(args):
                if skipCount: skipCount -= 1
                elif arg == "-o":
                    skipCount = 1
                    linkTarget.addDependency("$BUILT_PRODUCTS_DIR/libtest_support.a")
                elif arg == "$DEPENDS_ON_ARG":
                    skipCount = 1
                    dependencies.append(args[idx+1])
                elif arg == "$DEPENDS_ON":
                    skipCount = 2
                    dependencies.append(args[idx+1])
                elif arg in ["-arch"]:
                    skipCount = 1
                    nextArg = args[idx+1]
                    ldflags.append(arg)
                    ldflags.append(nextArg)
                    cflags.append(arg)
                    cflags.append(nextArg)
                elif arg in ["-target"]:
                    skipCount = 1
                    nextArg = args[idx+1]
                    ldflags.append(arg)
                    ldflags.append(nextArg)
                    cflags.append(arg)
                    cflags.append(nextArg)
                    if osFlag in nextArg:
                        minOS = nextArg[nextArg.find(osFlag)+len(osFlag):]
                    if "macabi" in nextArg: # Don't link test support for catalyst apps
                        linkTestSupport = False
                elif arg in ["-install_name","-framework", "-rpath","-compatibility_version","-sub_library", "-undefined", "-current_version"]:
                    skipCount = 1
                    nextArg = args[idx+1]
                    ldflags.append(arg)
                    ldflags.append(nextArg)
                elif arg == "-sectcreate":
                    skipCount = 3
                    ldflags.append(arg)
                    ldflags.append(args[idx+1])
                    ldflags.append(args[idx+2])
                    ldflags.append(args[idx+3])
                elif arg[:2] == "-L": ldflags.append(arg)
                elif arg[:2] == "-F": ldflags.append(arg)
                elif arg == "-nostdlib":
                    ldflags.append(arg)
                    # Kernel tests pass -nostdlib so don't link test support
                    linkTestSupport = False
                elif arg == "-flat_namespace":
                    ldflags.append(arg)
                elif arg in ["-dynamiclib","-bundle"]:
                    ldflags.append(arg)
                    isDylib = True
                elif arg in ["-ltest_support"]:
                    explicitLinkTestSupport = True
                elif arg.endswith((".s", ".c", ".cpp", ".cxx", ".m", ".mm")):
                    if not arg.startswith("$SRCROOT"): sources.append(testSrcDir + "/" + arg)
                    else: sources.append(arg)
                elif arg in targetNames:
                    linkTarget.addInput(arg)
                elif arg[:4] == "-Wl,":
                    linkerArgs = arg.split(",")
                    if linkerArgs[1] == "-platform_version":
                        minOS = linkerArgs[3]
                        platformVersion = arg
                    else:
                        for linkerArg in linkerArgs[1:]:
                            if linkerArg in targetNames: linkTarget.addDependency(linkerArg)
                        ldflags.append(arg)
                elif arg[:2] == "-l":
                    candidate = "{}/lib{}.dylib".format(testDstDir, arg[2:])
                    if candidate in targetNames: linkTarget.addDependency(candidate)
                    ldflags.append(arg)
                elif arg[:7] == "-weak-l":
                    candidate = "{}/lib{}.dylib".format(testDstDir, arg[7:])
                    if candidate in targetNames: linkTarget.addDependency(candidate)
                    ldflags.append(arg)
                elif arg[:9] == "-upward-l":
                    candidate = "{}/lib{}.dylib".format(testDstDir, arg[9:])
                    if candidate in targetNames: linkTarget.addDependency(candidate)
                    ldflags.append(arg)
                elif arg[:8] == "-fuse-ld":
                    # This is not typically used, but if we ever wanted to try a new ld64, it can be useful
                    ldflags.append(arg)
                elif arg.startswith('-fsanitize='):
                    cflags.append(arg)
                    ldflags.append(arg)
                else:
                    cflags.append(arg)

            if isDylib and not explicitLinkTestSupport:
                linkTestSupport = False;
            #Explicitly disable test support for older binaries
            if minOS and minOS < osVers:
                linkTestSupport = False

            if linkTestSupport:
                ldflags.append("-ltest_support -force_load $BUILT_PRODUCTS_DIR/libtest_support.a")
            else:
                cflags.append("-DUSE_PRINTF_MACROS")
            for source in sources:
                objectHash = hashlib.sha1((linkTarget.output+source+tool+"".join(cflags)).encode('utf-8')).hexdigest()
                target = ninja.newTarget(tool, "$OBJROOT/dyld_tests.build/Objects-normal/" + objectHash + ".o")
                target.addInput(source)
                target.dependencies = dependencies
                if cflags: target.addVariable("cflags", " ".join(cflags))
                if minOS: target.addVariable("minOS", "-target " + osFlag + minOS)
                if forceArchs: target.addVariable("archs", " ".join(["-arch {}".format(arch) for arch in forceArchs]))
                addEnvVarsTo(target)
                linkTarget.addInput(target)
            if ldflags: linkTarget.addVariable("ldflags", " ".join(ldflags))
            if platformVersion: linkTarget.addVariable("minOS", platformVersion)
            if forceArchs: linkTarget.addVariable("archs", " ".join(["-arch {}".format(arch) for arch in forceArchs]))
            installTarget = ninja.newTarget("install", "$INSTALL_DIR/AppleInternal/CoreOS/tests/dyld/{}".format(linkTarget.output[9:]))
            installTarget.addInput(linkTarget)
            testTarget.addInput(linkTarget)
            testInstallTarget.addInput(installTarget)
        elif args[0] == "$APP_CACHE_UTIL":
            tool = args[0][1:].lower()
            sources = []
            flags = []
            dependencies = []
            skipCount = 0
            linkTarget = None
            args = [escapedArg.replace("\"", "\\\"") for escapedArg in args[1:]]
            skipCount = 0
            for idx, arg in enumerate(args):
                if skipCount: skipCount -= 1
                elif arg == "$DEPENDS_ON_ARG":
                    skipCount = 1
                    dependencies.append(args[idx+1])
                elif arg == "-create-kernel-collection":
                    skipCount = 1
                    linkTarget = ninja.newTarget("app-cache-util", args[idx+1])
                    linkTarget.addVariable("create_kind", arg)
                    testTarget.addInput(linkTarget)
                    dependencies.append("$BUILT_PRODUCTS_DIR/host_tools/dyld_app_cache_util")
                elif arg == "-kernel":
                    skipCount = 1
                    linkTarget.addInput(args[idx+1])
                    flags.append(arg)
                    flags.append(args[idx+1])
                elif arg == "-create-aux-kernel-collection":
                    skipCount = 1
                    linkTarget = ninja.newTarget("app-cache-util", args[idx+1])
                    linkTarget.addVariable("create_kind", arg)
                    testTarget.addInput(linkTarget)
                    dependencies.append("$BUILT_PRODUCTS_DIR/host_tools/dyld_app_cache_util")
                elif arg == "-create-pageable-kernel-collection":
                    skipCount = 1
                    linkTarget = ninja.newTarget("app-cache-util", args[idx+1])
                    linkTarget.addVariable("create_kind", arg)
                    testTarget.addInput(linkTarget)
                    dependencies.append("$BUILT_PRODUCTS_DIR/host_tools/dyld_app_cache_util")
                elif arg == "-kernel-collection":
                    skipCount = 1
                    linkTarget.addInput(args[idx+1])
                    flags.append(arg)
                    flags.append(args[idx+1])
                elif arg == "-pageable-collection":
                    skipCount = 1
                    linkTarget.addInput(args[idx+1])
                    flags.append(arg)
                    flags.append(args[idx+1])
                else:
                    flags.append(arg)
            linkTarget.dependencies = dependencies
            if flags: linkTarget.addVariable("flags", " ".join(flags))
            if forceArchs: linkTarget.addVariable("archs", " ".join(["-arch {}".format(arch) for arch in forceArchs]))
            installTarget = ninja.newTarget("install", "$INSTALL_DIR/AppleInternal/CoreOS/tests/dyld/{}".format(linkTarget.output[9:]))
            installTarget.addInput(linkTarget)
            testTarget.addInput(linkTarget)
            testInstallTarget.addInput(installTarget)
        else: raise ValueError("Unknown Command: {}".format(args[0]))
        

def processRunLine(runFile, runLine, environment):
    if runLine.startswith("sudo "):
        runFile.write("sudo {} {}\n".format(environment, runLine[5:]))
    else:
        runFile.write("{} {}\n".format(environment, runLine))

def processRunLines(ninja, runLines, testName, platform, runStatic, symRoot, xcTestInvocations):
    runFilePath = "{}/{}/run.sh".format(symRoot, testName)
    for runLine in runLines:
        xcTestInvocations.append("{{ \"{}\", \"{}\" }}".format(testName, runLine.replace("\"","\\\"").replace("sudo","")))
    with BufferedFile(runFilePath) as runFile:
        runFile.write("#!/bin/sh\n")
        runFile.write("cd  {}\n".format(testRunDir))

        if runStatic:
            runFile.write("echo \"run static\" \n");
            for runLine in runLines:
                processRunLine(runFile, runLine, "TEST_OUTPUT=BATS")
        else:
            runFile.write("echo \"run with JustInTimeLoaders\" \n");
            for runLine in runLines:
                processRunLine(runFile, runLine, "TEST_OUTPUT=BATS DYLD_USE_CLOSURES=0")

            runFile.write("echo \"run with JustInTimeLoaders and write PrebuiltLoaderSet\" \n");
            for runLine in runLines:
                processRunLine(runFile, runLine, "TEST_OUTPUT=BATS DYLD_USE_CLOSURES=1")

            runFile.write("echo \"run with PrebuiltLoaderSet\" \n");
            for runLine in runLines:
                if runLine.startswith("sudo "):
                    runFile.write("sudo TEST_OUTPUT=BATS DYLD_USE_CLOSURES=2 {}\n".format(runLine[5:]))
                else:
                    runFile.write("TEST_OUTPUT=BATS DYLD_USE_CLOSURES=2 {}\n".format(runLine))
    os.chmod(runFilePath, 0o755)
    installPath = "$INSTALL_DIR/AppleInternal/CoreOS/tests/dyld/{}/run.sh".format(testName)
    target = ninja.newTarget("install", installPath)
    target.addInput(runFilePath)
    ninja.findTarget("install-{}".format(testName)).addInput(installPath)

# returns a tuple of:
# 1. Idx after end of directive
# 2. Set of platforms directive is restricted to
# 3. Bool indicatin if the directive has a platform specifier
def parseDirective(line, directive, platform, archs, explicitROS):
    idx = line.find(directive)
    if idx == -1: return -1, archs, False
    if line[idx + len(directive)] == ':': return idx+len(directive)+1, archs, False
    match = re.match("\((.*?)\)?(?:\|(.*?)\))?\:", line[idx + len(directive):]);
    if match:
        foundPlatform = False
        platforms = []
        restrictedArchs = []
        if match.group(1):
            foundPlatform = True
            platforms = match.group(1).split(",");
        if match.group(2): restrictedArchs = match.group(2).split(",");
        if platforms and platform not in platforms: return -1, archs, foundPlatform
        effectiveArchs = list(set(archs) & set(restrictedArchs))

        if effectiveArchs:
            return idx + len(directive) + len(match.group()), effectiveArchs, foundPlatform
        return line.find(':')+1, archs, foundPlatform
    return -1, archs, False

if __name__ == "__main__":
    configPath = sys.argv[1]
    configMap = {}
    with open(configPath, "r") as configFile:
        for line in configFile.read().splitlines():
            args = line.split()
            configMap[args[0]] = args[2:]
    sys.stderr.write("CONFIG: {}\n".format(configMap))
    srcRoot = configMap["SRCROOT"][0]
    symRoot = configMap["SYMROOT"][0]
    sdkRoot = configMap["SDKROOT"][0]
    dkSdkRoot = configMap["DK_SDKROOT"][0]
    objRoot = configMap["OBJROOT"][0]
    platform = configMap["OSPLATFORM"][0]
    osVers = configMap["OSVERSION"][0]
    linkerFlags = configMap["LDFLAGS"][0];
    dkLinkerFlags = configMap["DK_LDFLAGS"][0];
    installOwner = configMap["INSTALL_OWNER"][0];
    installGroup = configMap["INSTALL_GROUP"][0];
    installMode = configMap["INSTALL_MODE_FLAG"][0];
    installDir = configMap["INSTALL_DIR"][0];
    userHeaderSearchPaths = configMap["USER_HEADER_SEARCH_PATHS"]
    systemHeaderSearchPaths = configMap["SYSTEM_HEADER_SEARCH_PATHS"]
    dkSystemHeaderSearchPaths = configMap["DK_SYSTEM_HEADER_SEARCH_PATHS"]

    derivedFilesDir = configMap["DERIVED_FILES_DIR"][0]
    archs = configMap["ARCHS"]

    if not os.path.exists(derivedFilesDir): os.makedirs(derivedFilesDir)
    if not os.path.exists(objRoot): os.makedirs(objRoot)

    sys.stderr.write("srcRoot = {}\n".format(srcRoot))
    sys.stderr.write("sdkRoot = {}\n".format(sdkRoot))
    sys.stderr.write("objRoot = {}\n".format(objRoot))
    sys.stderr.write("platform = {}\n".format(platform))
    sys.stderr.write("osVers = {}\n".format(osVers))
    sys.stderr.write("archs = {}\n".format(archs))
    sys.stderr.write("derivedFilesDir = {}\n".format(derivedFilesDir))

    testSrcRoot = os.path.abspath(srcRoot + "/testing/test-cases")
    ccTool = os.popen("xcrun --sdk " + sdkRoot + " --find clang").read().rstrip()
    dkCcTool = os.popen("xcrun --sdk " + dkSdkRoot + " --find clang").read().rstrip()
    cxxTool = os.popen("xcrun --sdk " + sdkRoot + " --find clang++").read().rstrip()
    headerPaths = " -isysroot " + sdkRoot
    dkHeaderPaths = " -isysroot " + dkSdkRoot
    for headerPath in userHeaderSearchPaths:  headerPaths += " -I{}".format(headerPath)
    for headerPath in systemHeaderSearchPaths:  headerPaths += " -I{}".format(headerPath)
    for dkHeaderPath in userHeaderSearchPaths:  dkHeaderPaths += " -I{}".format(dkHeaderPath)
    for dkHeaderPath in dkSystemHeaderSearchPaths:  dkHeaderPaths += " -I{}".format(dkHeaderPath)

    sudoCmd = ""
    osFlag = ""
    if platform == "macosx":
        platform = "macos"
        osFlag = "apple-macos"
        sudoCmd = "sudo"
    elif platform == "iphoneos":
        platform = "ios"
        osFlag = "apple-ios"
    elif platform == "appletvos":
        platform = "tvos"
        osFlag = "apple-tvos"
    elif platform == "watchos":
        osFlag = "apple-watchos"
    elif platform == "bridgeos":
        osFlag = "apple-bridgeos"
    else:
        sys.stderr.write("Unknown platform\n")
        sys.exit(-1)

    # Find ASan dylib path
    clangRuntimeDir = subprocess.run(
        [ccTool, '--print-file-name=lib'],
        capture_output=True,
        text=True).stdout.strip()
    clangRuntimeDir = os.path.join(clangRuntimeDir, 'darwin')
    if not os.path.exists(clangRuntimeDir):
        sys.stderr.write(f"Path to Clang runtime dir doesn't exist: '{clangRuntimeDir}'")
        sys.exit(-1)
    if platform == 'macos':
        sanitizerPlatform = 'osx'
    else:
        sanitizerPlatform = platform
    asanDylibName = f"libclang_rt.asan_{sanitizerPlatform}_dynamic.dylib"
    asanDylibPath = os.path.join(clangRuntimeDir, asanDylibName)
    if not os.path.exists(asanDylibPath):
        sys.stderr.write(f"Path to ASan dylib doesn't exist: '{asanDylibPath}'")
        sys.exit(-1)


    with NinjaFile(derivedFilesDir + "/build.ninja") as ninja:
        extraCmds = "$extraCmds"
        if "RC_XBS" in os.environ and os.environ["RC_XBS"] == "YES":
            extraCmds = "&& dsymutil -o $out.dSYM $out $extraCmds"
        ninja.addInclude("config.ninja")
            ninja.addVariable("minOS", "-target " + osFlag + osVers)
        ninja.addVariable("dkMinOS", "-target apple-driverkit")
        ninja.addVariable("archs", " ".join(["-arch {}".format(arch) for arch in archs]))
        ninja.addVariable("mode", "0755")
        ninja.addVariable("headerpaths", headerPaths)
        ninja.addVariable("dkHeaderpaths", dkHeaderPaths)
        ninja.addVariable("asanDylibPath", asanDylibPath)
        ninja.addVariable("asanDylibName", asanDylibName)
        ninja.addVariable("clangRuntimeDir", clangRuntimeDir)

        ninja.addRule("cc", "{} -g -MMD -MF $out.d $archs -o $out -c $in $minOS $headerpaths $cflags".format(ccTool), "$out.d")
        ninja.addRule("dkcc", "{} -g -MMD -MF $out.d $archs -o $out -c $in $dkMinOS $dkHeaderpaths $cflags".format(dkCcTool), "$out.d")
        ninja.addRule("cxx", "{} -g -MMD -MF $out.d $archs -o $out -c $in $minOS $headerpaths $cflags".format(cxxTool), "$out.d")
        ninja.addRule("cc-link", "{}  -g $archs -o $out $in $minOS -isysroot {} {} $ldflags {} && codesign --force --sign - $entitlements $out".format(ccTool, sdkRoot, linkerFlags, extraCmds), False)
        ninja.addRule("dkcc-link", "{}  -g $archs -o $out $in $dkMinOS -isysroot {} {} $ldflags {} && codesign --force --sign - $entitlements $out".format(dkCcTool, dkSdkRoot, dkLinkerFlags, extraCmds), False)
        ninja.addRule("cxx-link", "{}  -g $archs -o $out $in $minOS -isysroot {} {} $ldflags {} && codesign --force --sign - $entitlements $out".format(cxxTool, sdkRoot, linkerFlags, extraCmds), False)
        ninja.addRule("app-cache-util", "$BUILT_PRODUCTS_DIR/host_tools/dyld_app_cache_util $archs $create_kind $out $flags", False, addEnvVars=False)
        ninja.addRule("dtrace", "/usr/sbin/dtrace -h -s $in -o $out", False)
        ninja.addRule("cp", "/usr/bin/ditto $in $out", False)
        ninja.addRule("install", "/usr/bin/install -m $mode -o {} -g {} $install_flags $in $out".format(installOwner, installGroup), False)
        ninja.addRule("symlink", "ln -sfh $source $out", False)

        allTarget = ninja.newTarget("phony", "all")
        masterInstallTarget = ninja.newTarget("phony", "install")

        runAllScriptPath = "{}/run_all_dyld_tests.sh".format(derivedFilesDir)
        xctestPath = "{}/dyld_xctest.h".format(derivedFilesDir)
        if "XCTestGenPath" in os.environ: xctestPath = os.environ["XCTestGenPath"]
        batsTests = []
        batsSuppressedCrashes = []
        xctestInvocations = []
        with BufferedFile(runAllScriptPath) as runAllScript:
            missingPlatformDirectives = False
            runAllScript.write("#!/bin/sh\n")
            for entry in os.listdir(testSrcRoot):
                if entry.endswith((".dtest")):
                    testName = entry[:-6]
                    sys.stdout.write("Processing " + testName + "\n")
                    runLines = []
                    runStaticLines = []
                    buildLines = []
                    effectiveArchs = archs

                    testSrcDir = "$SRCROOT/testing/test-cases/{}.dtest".format(testName)
                    testDstDir = "$SYMROOT/{}".format(testName)
                    testRunDir = "/AppleInternal/CoreOS/tests/dyld/{}".format(testName)

                    batsTest = {}
                    batsTest["TestName"] = testName
                    batsTest["Arch"] = "platform-native"
                    batsTest["WorkingDirectory"] = testRunDir
                    batsTest["ShowSubtestResults"] = True
                    batsTest["Command"] = []
                    batsTest["Command"].append("./run.sh")
                    for file in os.listdir(testSrcRoot + "/" + entry):
                        buildSubs = {
                            "BUILD_DIR":            testDstDir,
                            "RUN_DIR":              testRunDir,
                            "SRC_DIR":              testSrcDir
                        }
                        runSubs = {
                            "RUN_DIR":        testRunDir,
                            "SUDO":           sudoCmd,
                            "RUN_STATIC":     "/AppleInternal/CoreOS/tests/dyld/run-static",
                        }
                        if file.endswith((".c", ".cpp", ".cxx", ".m", ".mm")):
                            with open(testSrcRoot + "/" + entry + "/" + file) as f:
                                lines = f.read().splitlines()
                                requiresPlatformDirective = False
                                foundPlatformDirective = False
                                explicitROS = -1
                                for line in lines:
                                    idx, forceArchs, foundPlatform = parseDirective(line, "BUILD", platform, archs, explicitROS);
                                    if foundPlatform: requiresPlatformDirective = True
                                    if idx != -1:
                                        foundPlatformDirective = True
                                        effectiveArchs = forceArchs
                                        if line[idx:]: buildLines.append(string.Template(line[idx:]).safe_substitute(buildSubs))
                                        continue
                                    idx, _, _ = parseDirective(line, "RUN", platform, archs, explicitROS);
                                    if idx != -1:
                                        if "$SUDO" in line: batsTest["AsRoot"] = True
                                        runLines.append(string.Template(line[idx:]).safe_substitute(runSubs).lstrip())
                                        continue
                                    idx, _, _ = parseDirective(line,"RUN_STATIC", platform, archs, explicitROS)
                                    if idx != -1:
                                        runStaticLines.append(string.Template(line[idx:]).safe_substitute(runSubs).lstrip())
                                        continue
                                    idx, _, _ = parseDirective(line,"RUN_TIMEOUT", platform, archs, explicitROS)
                                    if idx != -1:
                                        batsTest["Timeout"] = line[idx:].lstrip()
                                        continue
                                    idx, _, _ = parseDirective(line,"BOOT_ARGS", platform, archs, explicitROS)
                                    if idx != -1:
                                        batsTest["BootArgsSet"] = ",".join(line[idx:].split())
                                        continue
                                    idx, _, _ = parseDirective(line,"NO_CRASH_LOG", platform, archs, explicitROS)
                                    if idx != -1:
                                        batsSuppressedCrashes.append(line[idx:].lstrip())
                                        continue
                                if requiresPlatformDirective and not foundPlatformDirective:
                                    missingPlatformDirectives = True
                                    sys.stderr.write("Did not find platform({}) BUILD directive for {}\n".format(platform, testName))
                    if buildLines and (runLines or runStaticLines):
                        processBuildLines(ninja, buildLines, testName, platform, osFlag, osVers, effectiveArchs, testDstDir, testSrcDir)
                        if runLines:
                            processRunLines(ninja, runLines, testName, platform, False, symRoot, xctestInvocations)
                        if runStaticLines:
                            processRunLines(ninja, runStaticLines, testName, platform, True, symRoot, xctestInvocations)
                        runAllScript.write("/AppleInternal/CoreOS/tests/dyld/{}/run.sh\n".format(testName))
                        batsTests.append(batsTest)
            if missingPlatformDirectives: sys.exit(-1)
            sys.stderr.write("Wrote test config to: {}\n".format(xctestPath))
            with BufferedFile(xctestPath) as xcTestFile:
                xcTestFile.write("static const TestInfo sTests[] = {\n")
                xcTestFile.write(",\n".join(xctestInvocations))
                xcTestFile.write("\n};")
        os.chmod(runAllScriptPath, 0o755)
        runAllFilesInstallTarget = ninja.newTarget("install", "$INSTALL_DIR/AppleInternal/CoreOS/tests/dyld/run_all_dyld_tests.sh")
        runAllFilesInstallTarget.addInput("$DERIVED_FILES_DIR/run_all_dyld_tests.sh")
        masterInstallTarget.addInput(runAllFilesInstallTarget)
        batsFilePath = derivedFilesDir + "/dyld.plist"
        batsTests.sort(key=lambda test: test["TestName"])
        with open(batsFilePath, "wb") as batsFile:
            batsConfig = { "BATSConfigVersion" : "0.1.0",
                       "Project"           : "dyld_tests",
                       "Tests"             : batsTests }
            if batsSuppressedCrashes: batsConfig["IgnoreCrashes"] = batsSuppressedCrashes
            plistlib.dump(batsConfig, batsFile, fmt=plistlib.FMT_BINARY)
        os.system('plutil -convert binary1 ' + batsFilePath) # convert the plist in place to binary
        batsConfigInstallTarget = ninja.newTarget("install", "$INSTALL_DIR/AppleInternal/CoreOS/BATS/unit_tests/dyld.plist")
        batsConfigInstallTarget.addInput(batsFilePath)
        batsConfigInstallTarget.addVariable("mode", "0644")
        masterInstallTarget.addInput(batsConfigInstallTarget)
    sys.stdout.write("DONE\n")

