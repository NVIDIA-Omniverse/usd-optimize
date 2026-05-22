import argparse
import glob
import os
import shutil
from typing import Callable, Dict

import omni.repo.man
import toml


def setup_repo_tool(parser: argparse.ArgumentParser, config: Dict) -> Callable:
    toolConfig = config.get("repo_py_package", {})
    if not toolConfig.get("enabled", True):
        return None

    parser.description = "Tool to build a wheel for the precompiled Scene Optimizer modules and all of its runtime dependencies."
    omni.repo.man.add_config_arg(parser)

    def run_repo_tool(_: Dict, config: Dict):
        toolConfig = config["repo_py_package"]
        stagingDir = toolConfig["staging_dir"]
        installDir = toolConfig["install_dir"]
        exclusions = toolConfig.get("exclude", [])
        ignore_callable = shutil.ignore_patterns(*exclusions)
        repoVersionFile = config["repo"]["folders"]["version_file"]
        fullVersion = omni.repo.man.build_number.generate_build_number_from_file(repoVersionFile)
        packageVersion, _ = fullVersion.split("+")

        # copy artifacts so they can be packaged by with a reasonable name
        source = omni.repo.man.resolve_tokens("_build/$platform/$config")
        if os.path.exists(stagingDir):
            shutil.rmtree(stagingDir)
        shutil.copytree(f"{source}/python/omni/scene/optimizer", f"{stagingDir}/omni/scene/optimizer", ignore=ignore_callable)
        # copy the libs on windows since they are needed for the wheel to work, but for Linux auditwheel will handle this
        if omni.repo.man.is_windows():
            shutil.copytree(f"{source}/lib", f"{stagingDir}/omniverse_scene_optimizer.libs", ignore=ignore_callable)
        # TODO: testing
        else:
            shutil.copytree(f"{source}/lib/operations", f"{stagingDir}/omniverse_scene_optimizer.libs/operations", ignore=ignore_callable)
            shutil.copyfile(f"{source}/lib/operation_mapping.json", f"{stagingDir}/omniverse_scene_optimizer.libs/operation_mapping.json")

        # generate pyproject file
        pyproject_source = omni.repo.man.resolve_tokens("$root/tools/pyproject/pyproject.toml")
        pyproject_target = f"{stagingDir}/pyproject.toml"
        with open(pyproject_source, "r") as f:
            data = toml.load(f)
        data["project"]["version"] = packageVersion
        with open(pyproject_target, "w") as f:
            toml.dump(data, f)

        # generate the README
        readme_source = omni.repo.man.resolve_tokens("$root/README.md")
        readme_target = f"{stagingDir}/README.md"
        with open(readme_source, "r") as f:
            data = f.readlines()
        with open(readme_target, "w") as f:
            f.writelines(data[4:7])

        # TODO: license files

        # copy the pyproject setup script
        shutil.copyfile(omni.repo.man.resolve_tokens("$root/tools/pyproject/pybuild.py"), f"{stagingDir}/pybuild.py")

        # build the wheel
        build_cmd = omni.repo.man.resolve_tokens("$root/tools/pyproject/pybuild${shell_ext}")
        build_args = [build_cmd, "build", "--format=wheel", f"--directory={stagingDir}", f"--output={stagingDir}/dist"]
        omni.repo.man.logger.info(" ".join(build_args))
        omni.repo.man.run_process(build_args, exit_on_error=True)

        wheel = glob.glob(f"{stagingDir}/dist/*.whl")[0]
        if omni.repo.man.is_windows():
            result = f"{installDir}/{os.path.basename(wheel)}"
            os.makedirs(os.path.dirname(result), exist_ok=True)
            shutil.copyfile(wheel, result)
            print(f"Packaged wheel installed to {result}")
        else:
            # repair the wheel by baking in the shared libraries
            tokens = omni.repo.man.get_tokens()
            platform_target_abi = omni.repo.man.get_abi_platform_translation(tokens["platform"], tokens.get("abi", "2.35"))
            env = os.environ.copy()
            env["LD_LIBRARY_PATH"] = f"{os.path.abspath(os.path.realpath(f'{source}/lib'))}:{os.path.abspath(os.path.realpath(f'{source}/extraLibs'))}"
            auditwheel_cmd = omni.repo.man.resolve_tokens("$root/tools/pyproject/auditwheel${shell_ext}")
            auditwheel_args = [auditwheel_cmd, "repair", wheel, "--plat", platform_target_abi, "-w", installDir]
            omni.repo.man.logger.info(" ".join(auditwheel_args))
            omni.repo.man.run_process(auditwheel_args, exit_on_error=True, env=env)

    return run_repo_tool
