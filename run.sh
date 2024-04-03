set -e
sudo losetup -f storage
name=$(losetup | grep storage | awk '{print $1;}')
sudo modprobe -r sblkdev
./mk.sh build
sudo ./mk.sh install
sudo modprobe sblkdev catalog="$name"
