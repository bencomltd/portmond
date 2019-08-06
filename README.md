Port Monitor Daemon is a simple daemon that monitors the Raspberry Pi gpio ports for any changes.
Any changes are sent to the web site in the config file using a http GET request.
At midnight, change the source if you wish to use a different time, it sends the state of all configured ports (Watchdog function).
How you use the output is up to the user. The GET request takes the form...
http://192.168.1.3/01-01-2019/12:00:00/port_num/state/ticks
sudo kill -sSIGUSR1 $pid Triggers a read of all configured ports. 

Compile the source Master Luke
cd ~/portmond
make
sudo make install
sudo systemctl enable portmond
sudo systemctl start portmond
tail -f /var/log/syslog

Build deb install package. May the force be with you, Master Luke.

sudo make build_deb

cd ..
cp portmond-1.0.deb to destination Raspberry Pi

Install
sudo dpkg -i portmond-1.0.deb
sudo systemctl enable portmond
sudo systemctl start portmond

Test
sudo tail -f /var/log/syslog
Trigger a gpio port change

Build Source Package
cd home/pi/portmond
edit portmond to your liking, compile and test

make clean

sudo make build_deb_src

cd ..
cp portmond-src-1.0.deb to destination Raspberry Pi

