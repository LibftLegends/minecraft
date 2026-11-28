#!/usr/bin/env sh

set -eu

printf 'Updating Libft from its tracked branch...\n'
git config submodule.Libft.url git@github.com:LibftLegends/Libft.git
git submodule sync -- Libft
git submodule update --init --remote --merge --recursive Libft
printf 'Libft submodule status:\n'
git submodule status --recursive Libft
