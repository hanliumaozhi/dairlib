# -*- mode: python -*-
# vi: set ft=python :

local_repository(
  name = "drake",
  path = "../drake",
)


# Reference external software libraries and tools per Drake's defaults.  Some
# software will come from the host system (Ubuntu or macOS); other software
# will be downloaded in source or binary form from github or other sites.
load("@drake//tools/workspace:default.bzl", "add_default_repositories")
add_default_repositories()

local_repository(
  name = "dircon",
  path = "../DIRCON",
)