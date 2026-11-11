About drekar-launch-process-cpp-feedstock
=========================================

Feedstock license: [BSD-3-Clause](https://github.com/johnwason/feedstock-feedstock/blob/main/LICENSE.txt)

Home: https://github.com/johnwason/drekar-launch-process-cpp

Package license: Apache-2.0

Summary: C++ client library for drekar-launch

Development: https://github.com/johnwason/drekar-launch-process-cpp

Current build status
====================


<table><tr>
    <td>GitHub Actions</td>
    <td>
      <a href="https://github.com/johnwason/feedstock-feedstock/actions/workflows/conda-build.yml">
        <img src="https://github.com/johnwason/feedstock-feedstock/actions/workflows/conda-build.yml/badge.svg?event=push&branch=main">
      </a>
    </td>
  </tr>
</table>

Current release info
====================

| Name | Downloads | Version | Platforms |
| --- | --- | --- | --- |
| [![Conda Recipe](https://img.shields.io/badge/recipe-drekar--launch--process--cpp-green.svg)](https://anaconda.org/wasontech/drekar-launch-process-cpp) | [![Conda Downloads](https://img.shields.io/conda/dn/wasontech/drekar-launch-process-cpp.svg)](https://anaconda.org/wasontech/drekar-launch-process-cpp) | [![Conda Version](https://img.shields.io/conda/vn/wasontech/drekar-launch-process-cpp.svg)](https://anaconda.org/wasontech/drekar-launch-process-cpp) | [![Conda Platforms](https://img.shields.io/conda/pn/wasontech/drekar-launch-process-cpp.svg)](https://anaconda.org/wasontech/drekar-launch-process-cpp) |

Installing drekar-launch-process-cpp
====================================

Installing `drekar-launch-process-cpp` from the `wasontech` channel can be achieved by adding `wasontech` to your channels with:

```
conda config --add channels wasontech
conda config --set channel_priority strict
```

How to use
----------

<details>
<summary>With conda</summary>

```
conda install drekar-launch-process-cpp
```

</details>

<details>
<summary>With mamba</summary>

```
mamba install drekar-launch-process-cpp
```

</details>

<details>
<summary>With pixi</summary>

```
# for adding to your local project
pixi add drekar-launch-process-cpp
# for installing globally
pixi global install drekar-launch-process-cpp
```

</details>

Search package versions
-----------------------

It is possible to list all of the versions of `drekar-launch-process-cpp` available on your platform:

<details>
<summary>With conda</summary>

```
conda search drekar-launch-process-cpp --channel wasontech
```

</details>

<details>
<summary>With mamba</summary>

```
mamba search drekar-launch-process-cpp --channel wasontech
```

</details>

<details>
<summary>With pixi</summary>

```
pixi search drekar-launch-process-cpp --channel wasontech
```

</details>

<details>
<summary>With mamba repoquery, which may provide more information</summary>

```
# Search all versions available on your platform:
mamba repoquery search drekar-launch-process-cpp --channel wasontech

# List packages depending on `drekar-launch-process-cpp`:
mamba repoquery whoneeds drekar-launch-process-cpp --channel wasontech

# List dependencies of `drekar-launch-process-cpp`:
mamba repoquery depends drekar-launch-process-cpp --channel wasontech
```

</details>




Updating drekar-launch-process-cpp-feedstock
============================================

If you would like to improve the drekar-launch-process-cpp recipe or build a new
package version, please fork this repository and submit a PR. Upon submission,
your changes will be run on the appropriate platforms to give the reviewer an
opportunity to confirm that the changes result in a successful build. Once
merged, the recipe will be re-built and uploaded automatically to the
`wasontech` channel, whereupon the built conda packages will be available for
everybody to install and use from the `wasontech` channel.
Note that all branches in the johnwason/drekar-launch-process-cpp-feedstock are
immediately built and any created packages are uploaded, so PRs should be based
on branches in forks, and branches in the main repository should only be used to
build distinct package versions.

In order to produce a uniquely identifiable distribution:
 * If the version of a package **is not** being increased, please add or increase
   the [``build/number``](https://docs.conda.io/projects/conda-build/en/latest/resources/define-metadata.html#build-number-and-string).
 * If the version of a package **is** being increased, please remember to return
   the [``build/number``](https://docs.conda.io/projects/conda-build/en/latest/resources/define-metadata.html#build-number-and-string)
   back to 0.

Feedstock Maintainers
=====================

* [@johnwason](https://github.com/johnwason/)

