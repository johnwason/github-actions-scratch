import argparse
import os
import re
from packaging import version

def main():
    parser = argparse.ArgumentParser(description="Compute semver for Robot Raconteur build")
    parser.add_argument("--github-env",action="store_true")

    args = parser.parse_args()

    with open("robotraconteur/RobotRaconteurCore/include/RobotRaconteur/RobotRaconteurConfig.h") as f:
        config_h = f.read()

    config_h_ver_str_m = re.search("ROBOTRACONTEUR_VERSION_TEXT \"(\\d+\\.\\d+\\.\\d+)\"", config_h)
    config_h_ver_str = config_h_ver_str_m.group(1)

    config_h_ver_regex = r"^refs\/tags\/v((?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*))?"
    assert re.match(config_h_ver_regex, config_h_ver_str), f"Invalid config header file version {config_h_ver_str}"

    ref = os.environ["GITHUB_REF"]
    if ref.startswith("refs/tags"):
        semver_tag_regex = r"^refs\/tags\/v((?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*))(-(?:alpha|beta|rc)\d+)?"
        m = re.match(semver_tag_regex,ref)
        assert m, f"Invalid tag {ref}"
        assert version.parse(m.group(2)) == version(config_h_ver_str)
        semver = m.group(1)
        print(semver)        
    else:
        run_id = os.environ["GITHUB_RUN_ID"]
        semver=config_h_ver_str + f"--dev{run_id}"
        print(semver)

    if args.github_env:
        github_env = os.environ["GITHUB_ENV"]
        with open(github_env, "r+") as f:
            f.write(f"ROBOTRACONTEUR_SEMVER={semver}\n")

if __name__ == "__main__":
    main()