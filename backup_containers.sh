#!/bin/bash

mkdir -p $HOME/container_backups
podman ps -q | xargs -I {} bash -c 'podman export {} -o $HOME/backups/{}-$(date +%Y%m%d-%H%M%S).tar'
podman ps -q | xargs -I {} bash -c 'podman inspect {} > $HOME/backups/{}-$(date +%Y%m%d-%H%M%S).json'
