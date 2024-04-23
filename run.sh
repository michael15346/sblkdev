sudo dmsetup remove my_basic_target_device
sudo modprobe -r sblkdev
sudo ./mk.sh build
sudo ./mk.sh install
sudo modprobe sblkdev
echo 0 122939392 basic_target /dev/sda4 0 | sudo dmsetup create my_basic_target_device
