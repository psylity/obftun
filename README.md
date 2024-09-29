# obftun - yet another obfuscated tunnel

The main purpose of this application is obfuscating traffic between two controlled servers. I personally use it to hide openvpn traffic, it is blocked in my country.

Real service packets are encapsulated into fake packets which are filled with random data and not-really-encrypted payload.

## Diagram

```
+-[ client machine ]---------------+                    +-[ server machine ]--------------+
|                                  |                    |                                 |
|          [client app]            |                    |        [ service:1194 ]         |
|               |                  |                    |               ^                 |
|               v                  |                    |               |                 |
|  [ obftun in client mode:1111 ]  | <-- (internet) --> |  [ obftun in server mode:2222]  |
|                                  |                    |                                 |
+----------------------------------+                    +------------------ 192.168.0.10 -+

$ head -2 /etc/openvpn/client.conf                      $ head -3 /etc/openvpn/server.conf
proto tcp-client                                        proto tcp-server
remote 127.0.0.1 1111                                   port 1194
                                                        local 127.0.0.1

$ cat /etc/obftun.conf                                  $ cat /etc/obftun.conf
client=true                                             server=true
bind="127.0.0.1:1111"                                   bind="192.168.0.10:2222"
peer="192.168.0.10:2222"                                peer="127.0.0.1:1194"
```

## Usage
```bash
$ obftun --help
Usage: obftun [OPTION...]
Another TCP/UDP tunnel to obfuscate the connection

  -b, --bind=ADDR:PORT       bind address. Default is 127.0.0.1:28726
  -c, --client               client mode.
  -C, --config=PATH          configuration file path. Default is
                             /etc/obftun.conf
  -p, --peer=ADDR:PORT       peer address.
  -s, --server               server mode.
  -t, --peer-tcp             connect to peer over tcp.
  -T, --bind-tcp             bind at tcp. This is default behaviour.
  -u, --peer-udp             connect to peer over udp. This is default
                             behavior.
  -U, --bind-udp             bind at udp
  -v, --verbose              verbose mode.
  -?, --help                 Give this help list
      --usage                Give a short usage message
  -V, --version              Print program version

Mandatory or optional arguments to long options are also mandatory or optional
for any corresponding short options.
```

## Compiling
```bash
$ mkdir build
$ cd build
$ cmake -DCMAKE_BUILD_TYPE=Release ..
-- The C compiler identification is GNU 11.4.0
-- The CXX compiler identification is GNU 11.4.0
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /usr/bin/cc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /usr/bin/c++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Configuring done
-- Generating done
-- Build files have been written to: /tmp/obftun/build
$ make
[ 20%] Building C object CMakeFiles/obftun.dir/main.c.o
[ 40%] Building C object CMakeFiles/obftun.dir/obfsm.c.o
[ 60%] Building C object CMakeFiles/obftun.dir/log.c.o
[ 80%] Building C object CMakeFiles/obftun.dir/tunnel.c.o
[100%] Linking C executable obftun
```
Since I didn't create .DEBs or RPMs, install it by hands:
```bash
$ sudo mv obftun /usr/bin/
$ sudo cp ../contrib/obftun.service /etc/systemd/system
$ sudo systemctl daemon-reload
$ sudo systemctl start obftun
$ sudo systemctl enable obftun
Created symlink /etc/systemd/system/multi-user.target.wants/obftun.service → /etc/systemd/system/obftun.service.
```

## TODO:
* Create .RPM & .DEB packages;
* Implement UDP mode;
* Proper MTU handling;
* Implement startup mode with heavy obfuscation;
  * Add random delays, more junk and empty packets during this stage;
* Add random discardable packets when there is no traffic or during startup;
* Do not place hdr1 at the fixed offset. Mark it;
* Merge payload with junk data more evenly;
* Add configurable secret which would protect tunnel in server mode - server will drop unauthorized connections; 
* Junk bytes entropy control:
    * junk generator with configurable entropy;
    * file where to take junk from;
* "Better" encryption - configurable XOR key :)
* Replace libconfig with something better.
