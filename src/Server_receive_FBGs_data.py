"""
File    : Server_receive_FBGS_data.py
Author  : Sooyeon Kim
Date    : September 15, 2023
Update  : 
Description : This script receives FBGs (Fiber Bragg Grating) force data from a client
              via TCP/IP, processes the data, and logs it to a CSV file.

Protocol    : Each packet consists of 11 bytes:
              - The first 3 bytes contain the channel ID and sensor ID.
              - The remaining 8 bytes contain the force data in double precision format.

              The script listens for data from a client, parses the received data, and
              logs the force readings for each sensor in the CSV file specified by
              `csv_file_path`.

Usage: Ensure that the `csv_file_path` points to the desired CSV file for logging.

Note: This script assumes that two channels (0 and 1) and two sensors (0 and 1) are
      being used. You may adjust the channel_num and id_val variables accordingly.
"""

import socket
import struct
import keyboard
import threading

######################################################
###################### SETTING #######################
### Manually edit
channel_num = [1, 2]  # (I4에서 오픈한 채널) 0~3

### Setting for TCP/IP communication
PORT = 4578
PACKET_SIZE = 11  # receiving 11 byte data

server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server_socket.bind(("0.0.0.0", PORT))
server_socket.listen(1)  # Listen for incoming connections
print("Server waiting for client connection...")

client_socket, addr = server_socket.accept()  # Accept a connection from a client
print(f"Connection from {addr}")

### Data
received_data = b''  # 빈 바이트 문자열로 초기화
id_val = [[channel_num[0], 0, 1], [channel_num[1], 0, 1]]  # [Channel, Sensor1, Sensor2]

force1 = 0
force2 = 0
force3 = 0
force4 = 0


######################################################
################ FUNCTION AND THREAD #################
### Thread for TCP communication
def receive_data():
    global received_data
    global force1, force2, force3, force4
    while True:
        chunk = client_socket.recv(PACKET_SIZE)
        if not chunk:
            # Client connection failed
            raise ConnectionError("Connection closed unexpectedly")
        received_data = chunk

        if len(received_data) >= PACKET_SIZE:
            id = struct.unpack('BBB', received_data[:3])
            FBGs = struct.unpack('d', received_data[3:])[0]

            if id[0] == id_val[0][0]:  # 1st channel
                if id[2] == id_val[0][1]:  # 1st sensor
                    force1 = FBGs
                elif id[2] == id_val[0][2]:  # 2nd sensor
                    force2 = FBGs
            elif id[0] == id_val[1][0]:  # 2nd channel
                if id[2] == id_val[1][1]:  # 1st sensor
                    force3 = FBGs
                elif id[2] == id_val[1][2]:  # 2nd sensor
                    force4 = FBGs
        ## Print received data
        print(f"{id[1]}:: Sensor ID: {id[2]}, Channel: {id[0]}, FBGs: {FBGs} nm")

######################################################
##################### EXECUTION ######################
### Thread
receive_thread = threading.Thread(target=receive_data)
receive_thread.start()

input("Press 'Enter' to continue").strip().lower()

while True:
    ## Quit
    if keyboard.is_pressed('esc'):
        print("Exit the loop")
        break

# Close the connection
client_socket.close()
server_socket.close()
