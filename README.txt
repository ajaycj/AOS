Author: AJAY 
Tested on : dc01 to dc45 university linux machines and on ubuntu 14.04
----------------------------------------------------------------------------------------------------------------------------
Detailed Instructions and Steps to compile and run the code.
----------------------------------------------------------------------------------------------------------------------------
-----------
Description
-----------
The project Involves the Implementattion of broadcast operation. The attached Zip folder has all the required files. The project implementation goes like this: A client program takes input from the user and pings to the node which should broadcasts message. The main file now initiates the broadcast, and comes to listening mode, ready to take next inputs.

-------------------
Instructions to Run
-------------------
1. Unzip the contents from the folder.
2. Use SCP to copy unzipped files in to CS machines (Files include: Config.txt, Launcher.sh, dist_node.c, data_initiator.c).
3. Compile the main code using the command " gcc -pthread -o dist_node dist_node.c"
4. Compile the client code using commnand "gcc -o data_initiator data_initiator.c"
4a. Set the broadcast initiator in the last line of the config file.
5. run the launcher script with the command "./launcher.sh"
6. This makes the nodes to run in different terminals.
7. Now select the node to broadcast and get its port number from config file, initiate client with the command    "./data_initiator machine_to_broadcast portnumber "Your Message"".
8. In order to send an another broadcast, follow step 7 with respective portnumber and message.
9. To terminate the processes running the script.
