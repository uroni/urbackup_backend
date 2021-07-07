
import Builder
import glob, os, sys

class ClangTidy(Builder.Action):
    def run(self, env):
        sh = env.shell
        clang_tidy = env.find_llvm_tool('clang-tidy')[0]
        if not clang_tidy:
            print("No clang-tidy executable could be found, installing...")
            sh.exec("sudo", "apt", "install", "-y", "clang-tidy-9")
            clang_tidy = env.find_llvm_tool('clang-tidy')[0]
            if not clang_tidy:
                print("No clang-tidy executable could be found")
                sys.exit(1)

        source_dir = sh.cwd()
        build_dir = os.path.join(source_dir, 'build')
        sources = [os.path.join(source_dir, file) for file in glob.glob(
            'source/**/*.c') if not ('windows' in file or 'android' in file)]

        return [
            Builder.DownloadDependencies(),
            Builder.CMakeBuild(),
            Builder.Script([
                [clang_tidy, '-p', build_dir] + sources
            ])
        ]
        
