set -e
sudo modprobe -r sblkdev
sudo ./mk.sh build
sudo ./mk.sh install
sudo modprobe sblkdev
