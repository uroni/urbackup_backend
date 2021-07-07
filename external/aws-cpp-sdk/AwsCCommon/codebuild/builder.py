# Copyright 2010-2017 Amazon.com, Inc. or its affiliates. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License").
# You may not use this file except in compliance with the License.
# A copy of the License is located at
#
#  http://aws.amazon.com/apache2.0
#
# or in the "license" file accompanying this file. This file is distributed
# on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
# express or implied. See the License for the specific language governing
# permissions and limitations under the License.

from __future__ import print_function
from importlib.abc import Loader, MetaPathFinder
import importlib
import platform, os, sys
import glob
import shutil
import subprocess
import tempfile


class BuildSpec(object):
    """ Refers to a specific build permutation, gets converted into a toolchain """

    def __init__(self, **kwargs):
        for slot in ('host', 'target', 'arch', 'compiler', 'compiler_version'):
            setattr(self, slot, 'default')
        self.downstream = False

        if 'spec' in kwargs:
            # Parse the spec from a single string
            self.host, self.compiler, self.compiler_version, self.target, self.arch, * \
                rest = kwargs['spec'].split('-')

            for variant in ('downstream',):
                if variant in rest:
                    setattr(self, variant, True)
                else:
                    setattr(self, variant, False)

        # Pull out individual fields. Note this is not in an else to support overriding at construction time
        for slot in ('host', 'target', 'arch', 'compiler', 'compiler_version'):
            if slot in kwargs:
                setattr(self, slot, kwargs[slot])

        self.name = '-'.join([self.host, self.compiler,
                              self.compiler_version, self.target, self.arch])
        if self.downstream:
            self.name += "-downstream"

    def __str__(self):
        return self.name

    def __repr__(self):
        return self.name


###############################################################################
# Virtual Module
# borrow the technique from the virtualmod module, allows 'import Builder' in
# .builder/*.py local scripts
###############################################################################
_virtual_modules = dict()


class VirtualModuleMetaclass(type):
    def __init__(cls, name, bases, attrs):
        # Initialize the class
        super(VirtualModuleMetaclass, cls).__init__(name, bases, attrs)

        # Do not register VirtualModule
        if name == 'VirtualModule':
            return

        module_name = getattr(cls, '__module_name__', cls.__name__) or name
        module = VirtualModule.create_module(module_name)

        # Copy over class attributes
        for key, value in attrs.items():
            if key in ('__name__', '__module_name__', '__module__', '__qualname__'):
                continue
            setattr(module, key, value)


class VirtualModule(metaclass=VirtualModuleMetaclass):
    class Finder(MetaPathFinder):
        def find_spec(fullname, path, target=None):
            if fullname in _virtual_modules:
                return _virtual_modules[fullname].__spec__
            return None

    class VirtualLoader(Loader):
        def create_module(spec):
            if spec.name not in _virtual_modules:
                return None

            return _virtual_modules[spec.name]

        def exec_module(module):
            module_name = module.__name__
            if hasattr(module, '__spec__'):
                module_name = module.__spec__.name

            sys.modules[module_name] = module

    @staticmethod
    def create_module(name):
        module_cls = type(sys)
        spec_cls = type(sys.__spec__)
        module = module_cls(name)
        setattr(module, '__spec__', spec_cls(
            name=name, loader=VirtualModule.VirtualLoader))
        _virtual_modules[name] = module
        return module


sys.meta_path.insert(0, VirtualModule.Finder)

########################################################################################################################
# DATA DEFINITIONS
########################################################################################################################

KEYS = {
    # Build
    'python': "",
    'c': None,
    'cxx': None,
    'pre_build_steps': [],
    'post_build_steps': [],
    'build_env': {},
    'cmake_args': [],
    'run_tests': True,
    'build': [],
    'test': [],

    # Linux
    'use_apt': False,
    'apt_keys': [],
    'apt_repos': [],
    'apt_packages': [],

    # macOS
    'use_brew': False,
    'brew_packages': [],

    # CodeBuild
    'enabled': True,
    'image': "",
    'image_type': "",
    'compute_type': "",
    'requires_privilege': False,
}

HOSTS = {
    'linux': {
        'architectures': {
            'x86': {
                'image': "123124136734.dkr.ecr.us-east-1.amazonaws.com/aws-common-runtime/ubuntu-16.04:x86",
            },
            'x64': {
                'image': "123124136734.dkr.ecr.us-east-1.amazonaws.com/aws-common-runtime/ubuntu-16.04:x64",
            },
        },

        'variables': {
            'python': "python3",
        },

        'cmake_args': [
            "-DPERFORM_HEADER_CHECK=ON",
        ],

        'use_apt': True,
        'apt_repos': [
            "ppa:ubuntu-toolchain-r/test",
        ],

        'image_type': "LINUX_CONTAINER",
        'compute_type': "BUILD_GENERAL1_SMALL",
    },
    'al2012': {
        'cmake_args': [
            "-DENABLE_SANITIZERS=OFF",
            "-DPERFORM_HEADER_CHECK=OFF",
        ],

        'variables': {
            'python': "python3",
        },

        'image': "123124136734.dkr.ecr.us-east-1.amazonaws.com/aws-common-runtime/al2012:x64",
        'image_type': "LINUX_CONTAINER",
        'compute_type': "BUILD_GENERAL1_SMALL",
    },
    'manylinux': {
        'architectures': {
            'x86': {
                'image': "123124136734.dkr.ecr.us-east-1.amazonaws.com/aws-common-runtime/manylinux1:x86",
            },
            'x64': {
                'image': "123124136734.dkr.ecr.us-east-1.amazonaws.com/aws-common-runtime/manylinux1:x64",
            },
        },

        'variables': {
            'python': "/opt/python/cp37-cp37m/bin/python",
        },

        'image_type': "LINUX_CONTAINER",
        'compute_type': "BUILD_GENERAL1_SMALL",
    },
    'windows': {
        'variables': {
            'python': "python.exe",
        },


        'cmake_args': [
            "-DPERFORM_HEADER_CHECK=ON",
        ],

        'image_type': "WINDOWS_CONTAINER",
        'compute_type': "BUILD_GENERAL1_MEDIUM",
    },
    'macos': {
        'variables': {
            'python': "python3",
        },

        'use_brew': True,
    }
}

TARGETS = {
    'linux': {
        'architectures': {
            'x86': {
                'cmake_args': [
                    '-DCMAKE_C_FLAGS=-m32',
                    '-DCMAKE_CXX_FLAGS=-m32',
                ],
            },
        },

        'cmake_args': [
            "-DENABLE_SANITIZERS=ON",
        ],
    },
    'macos': {
        'architectures': {
            'x86': {
                'cmake_args': [
                    '-DCMAKE_C_FLAGS=-m32',
                    '-DCMAKE_CXX_FLAGS=-m32',
                ],
            },
        },
    },
    'android': {
        'cmake_args': [
            "-DTARGET_ARCH=ANDROID",
            "-DCMAKE_TOOLCHAIN_FILE=/opt/android-ndk/build/cmake/android.toolchain.cmake",
            "-DANDROID_NDK=/opt/android-ndk",
        ],
        'run_tests': False,

        'architectures': {
            'arm64v8a': {
                'cmake_args': [
                    "-DANDROID_ABI=arm64-v8a",
                ],
            },
        },

        'image_type': "LINUX_CONTAINER",
        'compute_type': "BUILD_GENERAL1_SMALL",
    },
    'windows': {
        "variables": {
            "exe": ".exe"
        }
    },
}

COMPILERS = {
    'default': {
        'hosts': ['macos', 'al2012', 'manylinux'],
        'targets': ['macos', 'linux'],

        'versions': {
            'default': {}
        }
    },
    'clang': {
        'hosts': ['linux', 'macos'],
        'targets': ['linux', 'macos'],

        'cmake_args': ['-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DENABLE_FUZZ_TESTS=ON'],

        'apt_keys': ["http://apt.llvm.org/llvm-snapshot.gpg.key"],

        'versions': {
            '3': {
                '!post_build_steps': [],
                '!apt_repos': [],
                '!cmake_args': [],

                'apt_packages': ["clang-3.9"],
                'c': "clang-3.9",
                'cxx': "clang-3.9",
            },
            '6': {
                'apt_repos': [
                    "deb http://apt.llvm.org/xenial/ llvm-toolchain-xenial-6.0 main",
                ],
                'apt_packages': ["clang-6.0", "clang-tidy-6.0"],

                'c': "clang-6.0",
                'cxx': "clang-6.0",

                'requires_privilege': True,
            },
            '8': {
                'apt_repos': [
                    "deb http://apt.llvm.org/xenial/ llvm-toolchain-xenial-8 main",
                ],
                'apt_packages': ["clang-8", "clang-tidy-8"],

                'c': "clang-8",
                'cxx': "clang-8",

                'requires_privilege': True,
            },
            '9': {
                'apt_repos': [
                    "deb http://apt.llvm.org/xenial/ llvm-toolchain-xenial-9 main",
                ],
                'apt_packages': ["clang-9", "clang-tidy-9"],

                'c': "clang-9",
                'cxx': "clang-9",

                'requires_privilege': True,
            },
        },
    },
    'gcc': {
        'hosts': ['linux'],
        'targets': ['linux'],

        'c': "gcc-{version}",
        'cxx': "g++-{version}",
        'apt_packages': ["gcc-{version}", "g++-{version}"],

        'versions': {
            '4.8': {},
            '5': {},
            '6': {},
            '7': {},
            '8': {},
        },

        'architectures': {
            'x86': {
                'apt_packages': ["gcc-{version}-multilib", "g++-{version}-multilib"],
            },
        },
    },
    'msvc': {
        'hosts': ['windows'],
        'targets': ['windows'],

        'cmake_args': ["-G", "Visual Studio {generator_version}{generator_postfix}"],

        'versions': {
            '2015': {
                'variables': {
                    'generator_version': "14 2015",
                },

                'image': "123124136734.dkr.ecr.us-east-1.amazonaws.com/aws-common-runtime/win-vs2015:x64",
            },
            '2017': {
                'variables': {
                    'generator_version': "15 2017",
                },

                'image': "123124136734.dkr.ecr.us-east-1.amazonaws.com/aws-common-runtime/win-vs2017:x64",
            },
        },

        'architectures': {
            'x64': {
                'variables': {
                    'generator_postfix': " Win64",
                },
            },
        },
    },
    'ndk': {
        'hosts': ['linux'],
        'targets': ['android'],

        'versions': {
            '19': {
                'cmake_args': [
                    "-DANDROID_NATIVE_API_LEVEL=19",
                ],

                'image': "123124136734.dkr.ecr.us-east-1.amazonaws.com/android/ndk-r19c:latest",
            }
        }
    }
}

########################################################################################################################
# PRODUCE CONFIG
########################################################################################################################

# Ensure the combination of options specified are valid together


def validate_build(build_spec):

    assert build_spec.host in HOSTS, "Host name {} is invalid".format(
        build_spec.host)
    assert build_spec.target in TARGETS, "Target {} is invalid".format(
        build_spec.target)

    assert build_spec.compiler in COMPILERS, "Compiler {} is invalid".format(
        build_spec.compiler)
    compiler = COMPILERS[build_spec.compiler]

    assert build_spec.compiler_version in compiler['versions'], "Compiler version {} is invalid for compiler {}".format(
        build_spec.compiler_version, build_spec.compiler)

    supported_hosts = compiler['hosts']
    assert build_spec.host in supported_hosts, "Compiler {} does not support host {}".format(
        build_spec.compiler, build_spec.host)

    supported_targets = compiler['targets']
    assert build_spec.target in supported_targets, "Compiler {} does not support target {}".format(
        build_spec.compiler, build_spec.target)

# Moved outside merge_dicts to avoid variable shadowing


def _apply_value(obj, key, new_value):

    key_type = type(new_value)
    if key_type == list:
        # Apply the config's value before the existing list
        obj[key] = new_value + obj[key]

    elif key_type == dict:
        # Iterate each element and recursively apply the values
        for k, v in new_value.items():
            _apply_value(obj[key], k, v)

    else:
        # Unsupported type, just use it
        obj[key] = new_value

# Replace all variable strings with their values


def _replace_variables(value, variables):

    key_type = type(value)
    if key_type == str:

        # If the whole string is a variable, just replace it
        if value and value.rfind('{') == 0 and value.find('}') == len(value) - 1:
            return variables.get(value[1:-1], '')

        # Custom formatter for optional variables
        from string import Formatter

        class VariableFormatter(Formatter):
            def get_value(self, key, args, kwds):
                if isinstance(key, str):
                    return kwds.get(key, '')
                else:
                    return super().get_value(key, args, kwds)
        formatter = VariableFormatter()

        # Strings just do a format
        return formatter.format(value, **variables)

    elif key_type == list:
        # Update each element
        return [_replace_variables(e, variables) for e in value]

    elif key_type == dict:
        # Iterate each element and recursively apply the variables
        return dict([(key, _replace_variables(value, variables)) for (key, value) in value.items()])

    else:
        # Unsupported, just return it
        return value

# Traverse the configurations to produce one for the specified


def produce_config(build_spec, config_file, **additional_variables):

    validate_build(build_spec)

    defaults = {
        'hosts': HOSTS,
        'targets': TARGETS,
        'compilers': COMPILERS,
    }

    # Build the list of config options to poll
    configs = []

    # Processes a config object (could come from a file), searching for keys hosts, targets, and compilers
    def process_config(config):

        def process_element(map, element_name, instance):
            if not map:
                return

            element = map.get(element_name)
            if not element:
                return

            new_config = element.get(instance)
            if not new_config:
                return

            configs.append(new_config)

            config_archs = new_config.get('architectures')
            if config_archs:
                config_arch = config_archs.get(build_spec.arch)
                if config_arch:
                    configs.append(config_arch)

            return new_config

        process_element(config, 'hosts', build_spec.host)
        process_element(config, 'targets', build_spec.target)

        compiler = process_element(config, 'compilers', build_spec.compiler)
        process_element(compiler, 'versions', build_spec.compiler_version)

    # Process defaults first
    process_config(defaults)

    # then override with config file
    if config_file:
        if not os.path.exists(config_file):
            raise Exception(
                "Config file {} specified, but could not be found".format(config_file))

        import json
        with open(config_file, 'r') as config_fp:
            try:
                project_config = json.load(config_fp)
                process_config(project_config)
                if project_config not in configs:
                    configs.append(project_config)
            except Exception as e:
                print("Failed to parse config file", config_file, e)
                sys.exit(1)

    new_version = {
        'spec': build_spec,
    }
    # Iterate all keys and apply them
    for key, default in KEYS.items():
        new_version[key] = default

        for config in configs:
            override_key = '!' + key
            if override_key in config:
                # Handle overrides
                new_version[key] = config[override_key]

            elif key in config:
                # By default, merge all values (except strings)
                _apply_value(new_version, key, config[key])

    # Default variables
    replacements = {
        'host': build_spec.host,
        'compiler': build_spec.compiler,
        'version': build_spec.compiler_version,
        'target': build_spec.target,
        'arch': build_spec.arch,
        'cwd': os.getcwd(),
        **additional_variables,
    }

    # Pull variables from the configs
    for config in configs:
        if 'variables' in config:
            variables = config['variables']
            assert type(variables) == dict

            # Copy into the variables list
            for k, v in variables.items():
                replacements[k] = v

    # Post process
    new_version = _replace_variables(new_version, replacements)
    new_version['variables'] = replacements

    return new_version


########################################################################################################################
# ACTIONS
########################################################################################################################
class Builder(VirtualModule):
    """ The interface available to scripts that define projects, builds, actions, or configuration """
    # Must cache available actions or the GC will delete them
    all_actions = set()

    def __init__(self):
        Builder.all_actions = set(Builder.Action.__subclasses__())
        self._load_scripts()

    @staticmethod
    def _load_scripts():
        """ Loads all scripts from ${cwd}/.builder/**/*.py to make their classes available """
        import importlib.util

        if not os.path.isdir('.builder'):
            return

        scripts = glob.glob('.builder/**/*.py')
        for script in scripts:
            # Ensure that the import path includes the directory the script is in
            # so that relative imports work
            script_dir = os.path.dirname(script)
            if script_dir not in sys.path:
                sys.path.append(script_dir)
            print("Importing {}".format(os.path.abspath(script)), flush=True)

            name = os.path.split(script)[1].split('.')[0]
            spec = importlib.util.spec_from_file_location(name, script)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            actions = frozenset(Builder._find_actions())
            new_actions = actions.difference(Builder.all_actions)
            print("Imported {}".format(
                ', '.join([a.__name__ for a in new_actions])))
            Builder.all_actions.update(new_actions)

    @staticmethod
    def _find_actions():
        return Builder.Action.__subclasses__()

    @staticmethod
    def find_action(name):
        """ Finds any loaded action class by name and returns it """
        name = name.replace('-', '').lower()
        all_actions = Builder._find_actions()
        for action in all_actions:
            if action.__name__.lower() == name:
                return action

    @staticmethod
    def run_action(action, env):
        """ Runs an action, and any generated child actions recursively """
        action_type = type(action)
        if action_type is str:
            try:
                action_cls = Builder.find_action(action)
                action = action_cls()
            except:
                print("Unable to find action {} to run".format(action))
                all_actions = [a.__name__ for a in Builder._find_actions()]
                print("Available actions: \n\t{}".format(
                    '\n\t'.join(all_actions)))
                sys.exit(2)

        print("Running: {}".format(action), flush=True)
        children = action.run(env)
        if children:
            if not isinstance(children, list) and not isinstance(children, tuple):
                children = [children]
            for child in children:
                Builder.run_action(child, env)
        print("Finished: {}".format(action), flush=True)

    class Shell(object):
        """ Virtual shell that abstracts away dry run and tracks/logs state """

        def __init__(self, dryrun=False):
            # Used in dry-run builds to track simulated working directory
            self._cwd = os.getcwd()
            # pushd/popd stack
            self.dir_stack = []
            self.env_stack = []
            self.dryrun = dryrun

        def _flatten_command(self, *command):
            # Process out lists
            new_command = []

            def _proc_segment(command_segment):
                e_type = type(command_segment)
                if e_type == str:
                    new_command.append(command_segment)
                elif e_type == list or e_type == tuple:
                    for segment in command_segment:
                        _proc_segment(segment)
            _proc_segment(command)
            return new_command

        def _log_command(self, *command):
            print('>', subprocess.list2cmdline(
                self._flatten_command(*command)), flush=True)

        def _run_command(self, *command):
            self._log_command(*command)
            if not self.dryrun:
                subprocess.check_call(self._flatten_command(
                    *command), stdout=sys.stdout, stderr=sys.stderr)

        def _cd(self, directory):
            if self.dryrun:
                if os.path.isabs(directory) or directory.startswith('$'):
                    self._cwd = directory
                else:
                    self._cwd = os.path.join(self._cwd, directory)
            else:
                os.chdir(directory)

        def cd(self, directory):
            """ # Helper to run chdir regardless of dry run status """
            self._log_command("cd", directory)
            self._cd(directory)

        def pushd(self, directory):
            """ Equivalent to bash/zsh pushd """
            self._log_command("pushd", directory)
            self.dir_stack.append(self.cwd())
            self._cd(directory)

        def popd(self):
            """ Equivalent to bash/zsh popd """
            if len(self.dir_stack) > 0:
                self._log_command("popd", self.dir_stack[-1])
                self._cd(self.dir_stack[-1])
                self.dir_stack.pop()

        def mkdir(self, directory):
            """ Equivalent to mkdir -p $dir """
            self._log_command("mkdir", "-p", directory)
            if not self.dryrun:
                os.makedirs(directory, exist_ok=True)

        def mktemp(self):
            """ Makes and returns the path to a temp directory """
            if self.dryrun:
                return os.path.expandvars("$TEMP/build")

            return tempfile.mkdtemp()

        def cwd(self):
            """ Returns current working directory, accounting for dry-runs """
            if self.dryrun:
                return self._cwd
            else:
                return os.getcwd()

        def setenv(self, var, value):
            """ Set an environment variable """
            self._log_command(["export", "{}={}".format(var, value)])
            if not self.dryrun:
                os.environ[var] = value

        def getenv(self, var):
            """ Get an environment variable """
            return os.environ[var]

        def pushenv(self):
            """ Store the current environment on a stack, for restoration later """
            self._log_command(['pushenv'])
            self.env_stack.append(dict(os.environ))

        def popenv(self):
            """ Restore the environment to the state on the top of the stack """
            self._log_command(['popenv'])
            env = self.env_stack.pop()
            # clear out values that won't be overwritten
            for name, value in dict(os.environ).items():
                if name not in env:
                    del os.environ[name]
            # write the old env
            for name, value in env.items():
                os.environ[name] = value

        def rm(self, path):
            """ Remove a file or directory """
            self._log_command(["rm -rf", path])
            if not self.dryrun:
                try:
                    shutil.rmtree(path)
                except Exception as e:
                    print("Failed to delete dir {}: {}".format(path, e))

        def where(self, exe, path=None):
            """ Platform agnostic `where executable` command """
            if path is None:
                path = os.environ['PATH']
            paths = path.split(os.pathsep)
            extlist = ['']

            def is_executable(path):
                return os.path.isfile(path) and os.access(path, os.X_OK)
            if sys.platform == 'win32':
                pathext = os.environ['PATHEXT'].lower().split(os.pathsep)
                (base, ext) = os.path.splitext(exe)
                if ext.lower() not in pathext:
                    extlist = pathext
            for ext in extlist:
                exe_name = exe + ext
                for p in paths:
                    exe_path = os.path.join(p, exe_name)
                    if is_executable(exe_path):
                        return exe_path

            return None

        def exec(self, *command, **kwargs):
            """ Executes a shell command, or just logs it for dry runs """
            if kwargs.get('always', False):
                prev_dryrun = self.dryrun
                self.dryrun = False
                self._run_command(*command)
                self.dryrun = prev_dryrun
            else:
                self._run_command(*command)

    class Env(object):
        """ Encapsulates the environment in which the build is running """

        def __init__(self, config={}):
            self._projects = {}

            # DEFAULTS
            self.dryrun = False  # overwritten by config
            # default the branch to whatever the current dir+git says it is
            self.branch = self._get_git_branch()

            # OVERRIDES: copy incoming config, overwriting defaults
            for key, val in config.items():
                setattr(self, key, val)

            # make sure the shell is initialized
            if not hasattr(self, 'shell'):
                self.shell = Builder.Shell(self.dryrun)

            # default the project to whatever can be found
            if not hasattr(self, 'project'):
                self.project = self._default_project()

            # build environment set up
            self.source_dir = os.environ.get(
                "CODEBUILD_SRC_DIR", self.shell.cwd())
            self.build_dir = os.path.join(self.source_dir, 'build')
            self.deps_dir = os.path.join(self.build_dir, 'deps')
            self.install_dir = os.path.join(self.build_dir, 'install')

        @staticmethod
        def _get_git_branch():
            travis_pr_branch = os.environ.get("TRAVIS_PULL_REQUEST_BRANCH")
            if travis_pr_branch:
                print("Found branch:", travis_pr_branch)
                return travis_pr_branch

            github_ref = os.environ.get("GITHUB_REF")
            if github_ref:
                origin_str = "refs/heads/"
                if github_ref.startswith(origin_str):
                    branch = github_ref[len(origin_str):]
                    print("Found github ref:", branch)
                    return branch

            branches = subprocess.check_output(
                ["git", "branch", "-a", "--contains", "HEAD"]).decode("utf-8")
            branches = [branch.strip('*').strip()
                        for branch in branches.split('\n') if branch]

            print("Found branches:", branches)

            for branch in branches:
                if branch == "(no branch)":
                    continue

                origin_str = "remotes/origin/"
                if branch.startswith(origin_str):
                    branch = branch[len(origin_str):]

                return branch

            return None

        def _cache_project(self, project):
            self._projects[project.name] = project
            return project

        def _default_project(self):
            project = self._project_from_cwd()
            if project:
                return self._cache_project(project)
            if not self.args.project:
                print(
                    "Multiple projects available and no project (-p|--project) specified")
                print("Available projects:", ', '.join(
                    [p.__name__ for p in Builder.Project.__subclasses__()]))
                sys.exit(1)

            project_name = self.args.project
            projects = Builder.Project.__subclasses__()
            for project_cls in projects:
                if project_cls.__name__ == project_name:
                    project = project_cls()
                    project.path = self.shell.cwd()
                    return self._cache_project(project)
            print("Could not find project named {}".format(project_name))
            sys.exit(1)

        def _project_from_cwd(self, name_hint=None):
            project_config = None
            project_config_file = os.path.abspath("builder.json")
            if os.path.exists(project_config_file):
                import json
                with open(project_config_file, 'r') as config_fp:
                    try:
                        project_config = json.load(config_fp)
                        return self._cache_project(Builder.Project(**project_config, path=self.shell.cwd()))
                    except Exception as e:
                        print("Failed to parse config file",
                              project_config_file, e)
                        sys.exit(1)

            # load any builder scripts and check them
            Builder._load_scripts()
            projects = Builder.Project.__subclasses__()
            project_cls = None
            if len(projects) == 1:
                project_cls = projects[0]
            elif name_hint:  # if there are multiple projects, try to use the hint if there is one
                for p in projects:
                    if p.__name__ == name_hint:
                        project_cls = p

            if project_cls:
                project = project_cls()
                project.path = self.shell.cwd()
                return self._cache_project(project)

            return None

        def find_project(self, name):
            """ Finds a project, either on disk, or makes a virtual one to allow for acquisition """
            project = self._projects.get(name, None)
            if project:
                return project

            sh = self.shell
            search_dirs = (self.source_dir, os.path.join(self.deps_dir, name))

            for search_dir in search_dirs:
                if (os.path.basename(search_dir) == name) and os.path.isdir(search_dir):
                    sh.pushd(search_dir)
                    project = self._project_from_cwd(name)
                    if not project:  # no config file, but still exists
                        project = self._cache_project(
                            Builder.Project(name=name, path=search_dir))
                    sh.popd()

                    return project

            # Enough of a project to get started, note that this is not cached
            return Builder.Project(name=name)

        def _find_compiler_tool(self, name, versions):
            for version in versions:
                for pattern in ('{name}-{version}', '{name}-{version}.0'):
                    exe = pattern.format(name=name, version=version)
                    path = self.shell.where(exe)
                    if path:
                        return path, version
            return None, None

        def find_gcc_tool(self, name, version=None):
            """ Finds gcc, gcc-ld, gcc-ranlib, etc at a specific version, or the latest one available """
            versions = [version] if version else list(range(8, 5, -1))
            return self._find_compiler_tool(name, versions)

        def find_llvm_tool(self, name, version=None):
            """ Finds clang, clang-tidy, lld, etc at a specific version, or the latest one available """
            versions = [version] if version else list(range(10, 6, -1))
            return self._find_compiler_tool(name, versions)

    class Action(object):
        """ A build step """

        def run(self, env):
            pass

        def __str__(self):
            return self.__class__.__name__

    class Script(Action):
        """ A build step that runs a series of shell commands or python functions """

        def __init__(self, commands, **kwargs):
            self.commands = commands
            self.name = kwargs.get('name', self.__class__.__name__)

        def run(self, env):
            sh = env.shell

            def _expand_vars(cmd):
                cmd_type = type(cmd)
                if cmd_type == str:
                    cmd = _replace_variables(cmd, env.config['variables'])
                elif cmd_type == list:
                    cmd = [_replace_variables(sub, env.config['variables']) for sub in cmd]
                return cmd

            # Interpolate any variables
            self.commands = [_expand_vars(cmd) for cmd in self.commands]

            # Run each of the commands
            for cmd in self.commands:
                cmd_type = type(cmd)
                if cmd_type == str:
                    sh.exec(cmd)
                elif cmd_type == list:
                    sh.exec(*cmd)
                elif isinstance(cmd, Builder.Action):
                    Builder.run_action(cmd, env)
                elif callable(cmd):
                    return cmd(env)
                else:
                    print('Unknown script sub command: {}: {}', cmd_type, cmd)
                    sys.exit(4)

        def __str__(self):
            if len(self.commands) == 0:
                return 'Script({}): Empty'.format(self.name)
            cmds = []
            for cmd in self.commands:
                cmd_type = type(cmd)
                if cmd_type == str:
                    cmds.append(cmd)
                elif cmd_type == list:
                    cmds.append(' '.join(cmd))
                elif isinstance(cmd, Builder.Action):
                    cmds.append(str(cmd))
                elif callable(cmd):
                    cmds.append(cmd.__func__.__name__)
                else:
                    cmds.append("UNKNOWN: {}".format(cmd))
            return 'Script({}): (\n\t{}\n)'.format(self.name, '\n\t'.join(cmds))

    class Project(object):
        """ Describes a given library and its dependencies/consumers """

        def __init__(self, **kwargs):
            self.upstream = self.dependencies = [
                p['name'] for p in kwargs.get('upstream', [])]
            self.downstream = self.consumers = [
                p['name'] for p in kwargs.get('downstream', [])]
            self.account = kwargs.get('account', 'awslabs')
            self.name = kwargs['name']
            self.url = "https://github.com/{}/{}.git".format(
                self.account, self.name)
            self.path = kwargs.get('path', None)

        def __repr__(self):
            return "{}: {}".format(self.name, self.url)

    class Toolchain(object):
        """ Represents a compiler toolchain """

        def __init__(self, **kwargs):
            if 'default' in kwargs or len(kwargs) == 0:
                for slot in ('host', 'target', 'arch', 'compiler', 'compiler_version'):
                    setattr(self, slot, 'default')

            if 'spec' in kwargs:
                # Parse the spec from a single string
                self.host, self.compiler, self.compiler_version, self.target, self.arch, * \
                    rest = kwargs['spec'].split('-')

            # Pull out individual fields. Note this is not in an else to support overriding at construction time
            for slot in ('host', 'target', 'arch', 'compiler', 'compiler_version'):
                if slot in kwargs:
                    setattr(self, slot, kwargs[slot])

            self.name = '-'.join([self.host, self.compiler,
                                  self.compiler_version, self.target, self.arch])

        def compiler_path(self, env):
            if self.compiler == 'default':
                env_cc = os.environ.get('CC', None)
                if env_cc:
                    return env.shell.where(env_cc)
                return env.shell.where('cc')
            elif self.compiler == 'clang':
                return env.find_llvm_tool('clang', self.compiler_version if self.compiler_version != 'default' else None)[0]
            elif self.compiler == 'gcc':
                return env.find_gcc_tool('gcc', self.compiler_version if self.compiler_version != 'default' else None)[0]
            elif self.compiler == 'msvc':
                return env.shell.where('cl.exe')
            return None

        def __str__(self):
            return self.name

        def __repr__(self):
            return self.name

    class InstallTools(Action):
        """ Installs prerequisites to building """

        def run(self, env):
            config = env.config
            sh = env.shell

            if getattr(env.args, 'skip_install', False):
                return

            if config['use_apt']:
                # Install keys
                for key in config['apt_keys']:
                    sh.exec("sudo", "apt-key", "adv", "--fetch-keys", key)

                # Add APT repositories
                for repo in config['apt_repos']:
                    sh.exec("sudo", "apt-add-repository", repo)

                # Install packages
                if config['apt_packages']:
                    sh.exec("sudo", "apt-get", "-qq", "update", "-y")
                    sh.exec("sudo", "apt-get", "-qq", "install",
                            "-y", "-f", config['apt_packages'])

            if config['use_brew']:
                for package in config['brew_packages']:
                    sh.exec("brew", "install", package)

    class DownloadDependencies(Action):
        """ Downloads the source for dependencies and consumers if necessary """

        def run(self, env):
            project = env.project
            sh = env.shell
            branch = env.branch
            deps = list(project.upstream)

            config = getattr(env, 'config', {})
            spec = config.get('spec', None)
            if spec and spec.downstream:
                deps += project.downstream

            if deps:
                sh.rm(env.deps_dir)
                sh.mkdir(env.deps_dir)
                sh.pushd(env.deps_dir)

                while deps:
                    dep = deps.pop()
                    dep_proj = env.find_project(dep)
                    if dep_proj.path:
                        continue

                    sh.exec("git", "clone", dep_proj.url, always=True)
                    sh.pushd(dep_proj.name)
                    try:
                        sh.exec("git", "checkout", branch, always=True)
                    except:
                        print("Project {} does not have a branch named {}, using master".format(
                            dep_proj.name, branch))

                    # load project, collect transitive dependencies/consumers
                    dep_proj = env.find_project(dep)
                    deps += dep_proj.upstream
                    if spec and spec.downstream:
                        deps += dep_proj.downstream
                    sh.popd()

                sh.popd()

    class CMakeBuild(Action):
        """ Runs cmake configure, build """

        def run(self, env):
            try:
                toolchain = env.toolchain
            except:
                try:
                    toolchain = env.toolchain = Builder.Toolchain(
                        spec=env.args.build)
                except:
                    toolchain = env.toolchain = Builder.Toolchain(default=True)

            sh = env.shell

            # TODO These platforms don't succeed when doing a RelWithDebInfo build
            build_config = env.args.config
            if toolchain.host in ("al2012", "manylinux"):
                build_config = "Debug"

            source_dir = env.source_dir
            build_dir = env.build_dir
            deps_dir = env.deps_dir
            install_dir = env.install_dir

            for d in (build_dir, deps_dir, install_dir):
                sh.mkdir(d)

            config = getattr(env, 'config', {})
            env.build_tests = config.get('build_tests', True)

            def build_project(project, build_tests=False):
                # build dependencies first, let cmake decide what needs doing
                for dep in [env.find_project(p) for p in project.upstream]:
                    sh.pushd(dep.path)
                    build_project(dep)
                    sh.popd()

                project_source_dir = project.path
                project_build_dir = os.path.join(project_source_dir, 'build')
                sh.mkdir(project_build_dir)
                sh.pushd(project_build_dir)

                # Set compiler flags
                compiler_flags = []
                if toolchain.compiler != 'default':
                    compiler_path = toolchain.compiler_path(env)
                    if compiler_path:
                        for opt in ['c', 'cxx']:
                            compiler_flags.append(
                                '-DCMAKE_{}_COMPILER={}'.format(opt.upper(), compiler_path))

                    if config:
                        for opt, variable in {'c': 'CC', 'cxx': 'CXX'}.items():
                            if opt in config and config[opt]:
                                sh.setenv(variable, config[opt])

                cmake_args = [
                    "-Werror=dev",
                    "-Werror=deprecated",
                    "-DCMAKE_INSTALL_PREFIX=" + install_dir,
                    "-DCMAKE_PREFIX_PATH=" + install_dir,
                    # Each image has a custom installed openssl build, make sure CMake knows where to find it
                    "-DLibCrypto_INCLUDE_DIR=/opt/openssl/include",
                    "-DLibCrypto_SHARED_LIBRARY=/opt/openssl/lib/libcrypto.so",
                    "-DLibCrypto_STATIC_LIBRARY=/opt/openssl/lib/libcrypto.a",
                    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                    "-DCMAKE_BUILD_TYPE=" + build_config,
                    "-DBUILD_TESTING=" + ("ON" if build_tests else "OFF"),
                ] + compiler_flags + getattr(project, 'cmake_args', []) + getattr(config, 'cmake_args', [])

                # configure
                sh.exec("cmake", cmake_args, project_source_dir)

                # build
                sh.exec("cmake", "--build", ".", "--config", build_config)

                # install
                sh.exec("cmake", "--build", ".", "--config",
                        build_config, "--target", "install")

                sh.popd()

            def build_projects(projects):
                sh.pushd(deps_dir)

                for proj in projects:
                    project = env.find_project(proj)
                    sh.pushd(project.path)
                    build_project(project)
                    sh.popd()

                sh.popd()

            sh.pushd(source_dir)

            build_projects(env.project.upstream)

            # BUILD
            build_project(env.project, getattr(env, 'build_tests', False))

            spec = getattr(env, 'build_spec', None)
            if spec and spec.downstream:
                build_projects(env.project.downstream)

            sh.popd()

    class CTestRun(Action):
        """ Uses ctest to run tests if tests are enabled/built via 'build_tests' """

        def run(self, env):
            has_tests = getattr(env, 'build_tests', False)
            if not has_tests:
                print("No tests were built, skipping test run")
                return

            sh = env.shell

            project_source_dir = sh.cwd()
            project_build_dir = os.path.join(project_source_dir, 'build')
            sh.pushd(project_build_dir)

            sh.exec("ctest", "--output-on-failure")

            sh.popd()


########################################################################################################################
# RUN BUILD
########################################################################################################################
def run_build(build_spec, env):

    build_action = Builder.CMakeBuild()
    test_action = Builder.CTestRun()

    prebuild_action = Builder.Script(config.get(
        'pre_build_steps', []), name='pre_build_steps')
    postbuild_action = Builder.Script(config.get(
        'post_build_steps', []), name='post_build_steps')

    build_steps = config.get('build', None)
    if build_steps:
        build_action = Builder.Script(build_steps, name='build')

    test_steps = config.get('test', None)
    if test_steps:
        test_action = Builder.Script(test_steps, name='test')

    # Set build environment
    env.shell.pushenv()
    for var, value in config.get('build_env', {}).items():
        env.shell.setenv(var, value)

    Builder.run_action(
        Builder.Script([
            Builder.InstallTools(),
            Builder.DownloadDependencies(),
            prebuild_action,
            build_action,
            postbuild_action,
            test_action,
        ], name='run_build'),
        env
    )

    env.shell.popenv()

########################################################################################################################
# CODEBUILD
########################################################################################################################


CODEBUILD_OVERRIDES = {
    'linux-clang-3-linux-x64': ['linux-clang3-x64'],
    'linux-clang-6-linux-x64': ['linux-clang6-x64'],
    'linux-clang-8-linux-x64': ['linux-clang8-x64'],
    'linux-clang-6-linux-x64-downstream': ['downstream'],

    'linux-gcc-4.8-linux-x86': ['linux-gcc-4x-x86', 'linux-gcc-4-linux-x86'],
    'linux-gcc-4.8-linux-x64': ['linux-gcc-4x-x64', 'linux-gcc-4-linux-x64'],
    'linux-gcc-5-linux-x64': ['linux-gcc-5x-x64'],
    'linux-gcc-6-linux-x64': ['linux-gcc-6x-x64'],
    'linux-gcc-7-linux-x64': ['linux-gcc-7x-x64'],
    'linux-gcc-8-linux-x64': [],

    'linux-ndk-19-android-arm64v8a': ['android-arm64-v8a'],

    'al2012-default-default-linux-x64': ["AL2012-gcc44"],

    'manylinux-default-default-linux-x86': ["ancient-linux-x86"],
    'manylinux-default-default-linux-x64': ["ancient-linux-x64"],

    'windows-msvc-2015-windows-x86': ['windows-msvc-2015-x86'],
    'windows-msvc-2015-windows-x64': ['windows-msvc-2015'],
    'windows-msvc-2017-windows-x64': ['windows-msvc-2017'],
}


def create_codebuild_project(config, project, github_account, inplace_script):

    variables = {
        'project': project,
        'account': github_account,
        'spec': config['spec'].name,
        'python': config['python'],
    }

    if inplace_script:
        run_commands = ["{python} ./codebuild/builder.py build {spec}"]
    else:
        run_commands = [
            "{python} -c \\\"from urllib.request import urlretrieve; urlretrieve('https://raw.githubusercontent.com/awslabs/aws-c-common/master/codebuild/builder.py', 'builder.py')\\\"",
            "{python} builder.py build {spec}"
        ]

    # This matches the CodeBuild API for expected format
    CREATE_PARAM_TEMPLATE = {
        'name': '{project}-{spec}',
        'source': {
            'type': 'GITHUB',
            'location': 'https://github.com/{account}/{project}.git',
            'gitCloneDepth': 1,
            'buildspec':
                'version: 0.2\n' +
                'phases:\n' +
                '  build:\n' +
                '    commands:\n' +
                '      - "{python} --version"\n' +
                '\n'.join(['      - "{}"'.format(command)
                           for command in run_commands]),
            'auth': {
                'type': 'OAUTH',
            },
            'reportBuildStatus': True,
        },
        'artifacts': {
            'type': 'NO_ARTIFACTS',
        },
        'environment': {
            'type': config['image_type'],
            'image': config['image'],
            'computeType': config['compute_type'],
            'privilegedMode': config['requires_privilege'],
        },
        'serviceRole': 'arn:aws:iam::123124136734:role/CodeBuildServiceRole',
        'badgeEnabled': False,
    }

    return _replace_variables(CREATE_PARAM_TEMPLATE, variables)

########################################################################################################################
# MAIN
########################################################################################################################

def default_spec(env):

    compiler = 'gcc'
    version = 'default'
    target = host = 'default'

    arch = ('x64' if sys.maxsize > 2**32 else 'x86')

    if sys.platform in ('linux', 'linux2'):
        target = host = 'linux'
        clang_path, clang_version = env.find_llvm_tool('clang')
        gcc_path, gcc_version = env.find_gcc_tool('gcc')
        if clang_path:
            print('Found clang {} as default compiler'.format(clang_version))
            compiler = 'clang'
            version = clang_version
        elif gcc_path:
            print('Found gcc {} as default compiler'.format(gcc_version))
            compiler = 'gcc'
            version = gcc_version
        else:
            print('Neither GCC or Clang could be found on this system, perhaps not installed yet?')

        if os.uname()[4][:3].startswith('arm'):
            arch = ('armv8' if sys.maxsize > 2**32 else 'armv7')

    elif sys.platform in ('win32'):
        target = host = 'windows'
        compiler = 'msvc'
    elif sys.platform in ('darwin'):
        target = host = 'macos'
        compiler = 'clang'

    return BuildSpec(host=host, compiler=compiler, compiler_version='{}'.format(version), target=target, arch=arch)

if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument('-d', '--dry-run', action='store_true',
                        help="Don't run the build, just print the commands that would run")
    parser.add_argument('-p', '--project', action='store',
                        type=str, help="Project to work on")
    parser.add_argument('--config', type=str, default='RelWithDebInfo',
                        help='The native code configuration to build with')
    parser.add_argument('--dump-config', action='store_true',
                        help="Print the config in use before running a build")
    parser.add_argument('--spec', type=str, dest='build')
    commands = parser.add_subparsers(dest='command')

    build = commands.add_parser(
        'build', help="Run target build, formatted 'host-compiler-compilerversion-target-arch'. Ex: linux-ndk-19-android-arm64v8a")
    build.add_argument('build', type=str, default='default')
    build.add_argument('--skip-install', action='store_true',
                       help="Skip the install phase, useful when testing locally")

    run = commands.add_parser('run', help='Run action. Ex: do-thing')
    run.add_argument('run', type=str)
    run.add_argument('args', nargs=argparse.REMAINDER)

    codebuild = commands.add_parser('codebuild', help="Create codebuild jobs")
    codebuild.add_argument(
        'project', type=str, help='The name of the repo to create the projects for')
    codebuild.add_argument('--github-account', type=str, dest='github_account',
                           default='awslabs', help='The GitHub account that owns the repo')
    codebuild.add_argument('--profile', type=str, default='default',
                           help='The profile in ~/.aws/credentials to use when creating the jobs')
    codebuild.add_argument('--inplace-script', action='store_true',
                           help='Use the python script in codebuild/builder.py instead of downloading it')
    codebuild.add_argument(
        '--config', type=str, help='The config file to use when generating the projects')

    args = parser.parse_args()

    # set up builder and environment
    builder = Builder()
    env = Builder.Env({
        'dryrun': args.dry_run,
        'args': args
    })

    # Build the config object
    config_file = os.path.join(env.shell.cwd(), "builder.json")
    build_name = getattr(args, 'build', None)
    if build_name:
        build_spec = env.build_spec = BuildSpec(spec=build_name)
    else:
        build_spec = env.build_spec = default_spec(env)
    config = env.config = produce_config(build_spec, config_file)
    if not env.config['enabled']:
        raise Exception("The project is disabled in this configuration")

    if getattr(args, 'dump_config', False):
        from pprint import pprint
        pprint(config)

    # Run a build with a specific spec/toolchain
    if args.command == 'build':
        print("Running build", build_spec.name, flush=True)
        run_build(build_spec, env)

    # run a single action, usually local to a project
    elif args.command == 'run':
        action = args.run
        builder.run_action(action, env)

    # generate/update codebuild jobs
    elif args.command == 'codebuild':

        # Setup AWS connection
        import boto3
        session = boto3.Session(
            profile_name=args.profile, region_name='us-east-1')
        codebuild = session.client('codebuild')

        # Get project status

        existing_projects = []
        new_projects = []

        project_prefix_len = len(args.project) + 1

        # Map of canonical builds to their existing codebuild projects (None if creation required)
        canonical_list = {key: None for key in CODEBUILD_OVERRIDES.keys()}
        # List of all potential names to search for
        all_potential_builds = list(CODEBUILD_OVERRIDES.keys())
        # Reverse mapping of codebuild name to canonical name
        full_codebuild_to_canonical = {key.replace(
            '.', ''): key for key in CODEBUILD_OVERRIDES.keys()}
        for canonical, cb_list in CODEBUILD_OVERRIDES.items():
            all_potential_builds += cb_list
            for cb in cb_list:
                full_codebuild_to_canonical[cb] = canonical

        # Search for the projects
        full_project_names = [
            '{}-{}'.format(args.project, build.replace('.', '')) for build in all_potential_builds]
        old_projects_response = codebuild.batch_get_projects(
            names=full_project_names)
        existing_projects += [project['name'][project_prefix_len:]
                              for project in old_projects_response['projects']]

        # Mark the found projects with their found names
        for project in existing_projects:
            canonical = full_codebuild_to_canonical[project]
            canonical_list[canonical] = project

        # Update all existing projects
        for canonical, cb_name in canonical_list.items():
            if cb_name:
                create = False
            else:
                cb_name = canonical
                create = True

            build_name = '{}-{}'.format(args.project, cb_name)

            build_spec = BuildSpec(spec=canonical)
            config = produce_config(build_spec, args.config)
            if not config['enabled']:
                print("Skipping spec {}, as it's disabled".format(build_spec.name))
                continue

            cb_project = create_codebuild_project(
                config, args.project, args.github_account, args.inplace_script)
            cb_project['name'] = build_name.replace('.', '')

            if create:
                print('Creating: {}'.format(canonical))
                if not args.dry_run:
                    codebuild.create_project(**cb_project)
            else:
                print('Updating: {} ({})'.format(canonical, cb_name))
                if not args.dry_run:
                    codebuild.update_project(**cb_project)
