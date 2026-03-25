#!/bin/bash

crontab -l; echo "0 6 * * * $($HOME)/backup_containers.sh" | crontab -