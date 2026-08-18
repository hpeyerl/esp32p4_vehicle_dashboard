#!/bin/sh
# Fresh-flash bootstrap — run me first on the box after flashing the image.
# Installs Ansible, then runs the local provisioning playbook (no control node, no ssh).
set -e
cd "$(dirname "$0")"

echo ">>> apt-get update (a freshly flashed image has an empty package cache)"
sudo apt-get update

echo ">>> installing ansible-core"
sudo apt-get install -y ansible-core || sudo apt-get install -y ansible

echo ">>> running provision.yml locally"
ansible-playbook -i 'localhost,' -c local provision.yml

cat <<'EOF'

============================================================
 Provisioning complete.
 REBOOT now to apply the stacked DTB (panel + CAN) and start
 the dashboard:

     sudo reboot
============================================================
EOF
