# rfis.py
# Python code for the Robotic Foram Imaging System
#
# This module contains all the Python stuff: the API implementation, 
# sequencing engine, support classes and constants.
#
# When run as a module, this script calls the run() method on a new 
# CommProcess, which acts as an independent communcation relay. In this
# role, it can also maintain system state information.
#
# Communication from MATLAB to both the microcontroller and CommProcess 
# is carried out over a socket, exposed by the API send() method. Usage 
# of this method is valid after a successful connect() call, which 
# either spawns a new CommProcess (if the socket connection fails), or 
# (re)establishes a connection to a running CommProcess.
#
# The final role of CommProcess is to start and control a ProgramThread
# which is used to implement automation. Queues are used as
# communication channels between CommProcess and its ProgramThread.
# Programs can be started from MATLAB by calling the API method
# do_program().
#
# The purpose of this somewhat convoluted architecture is to simplify 
# the API, and maintain well-defined separation of functionality.
#
# Author: Eric Davis (ecdavis@ncsu.edu)

#import asyncio
#import serial_asyncio #nevermind, no windows support (TODO: check again - someone forked and updated it.)
import sys #for byteorder
#import binascii #for byte string printing (leave me alone, unicode)
import datetime #for logs

import serial
from serial.tools import list_ports
import socket
import select
#import matlab.engine #can't do this here - problems when API is instantiated from w/in MATLAB (can't have engine in itself)

#for API.connect()/CommProcess
import subprocess

#for action sequencing programs:
import time
import threading
import json
import queue

#default log file names
RFIS_PROCESS_LOG="rfis_proc.log"

#communication constants
SERIAL_BAUD_RATE=115200
SERIAL_PORT_DEFAULT="COM9"
SOCKET_PORT_DEFAULT=12345
MATLAB_NAME="RFIS" #name of shared engine

#indicate message types to MATLAB
MSG_TYPE_MCU = 0 #4 byte message from the microcontroller
MSG_TYPE_SEQ = 1 #JSON from the sequencer
MSG_TYPE_COM = 2 #control data from the comms process
MSG_TYPE_API = 3
MSG_TYPE_GUI = 4

#mechanical constants
MOTOR_COUNT=5

#for array indexing
MOTOR_1=0
MOTOR_2=1
MOTOR_3=2
MOTOR_4=3
MOTOR_5=4

#for making sense
MOTOR_1_FUNCTION="Imaging Pin"
MOTOR_2_FUNCTION="Isolation Pin"
MOTOR_3_FUNCTION="Stage Translation"
MOTOR_4_FUNCTION="Arm Translation"
MOTOR_5_FUNCTION="Arm Extension"


#kept in HardwareState
#represents state for a motor (as reported by encoder)
class MotorState:
    def __init__(self):
        self.step_per_dist=1 #conversion between steps and linear distance of attached mechanism
        self.pos=0 #current position
        self.maxpos=0 #limits
        self.minpos=0
        self.target_pos=0 #goal position
        self.x=1

    def pos_to_mm(self):
        return self.pos*self.step_per_dist

    def mm_to_pos(self,mm):
        return self.pos/self.self.step_per_dist


#kept in CommProcess
#maintains overall state of hardware
class HardwareState:
    def __init__(self):
        self.motors=[MotorState() for ii in range(MOTOR_COUNT)]
        

# --- INCOMPLETE --- don't take the following as authoritative
# Action programs are defined as JSON arrays.
#
# The first element is always an object containing zero or more fields 
# for program metadata.
#   Defined fields are as follows:
#    "description"
#      (optional) String used to describe entire program.
#    "symbols"
#      (optional) Object containing zero or more key-value pairs
#      defining the default value of symbols.
#    Any other fields are ignored.
#
#  Symbolic values are always numeric, and should be constrained
#  according to their usage. (Ex. Positions for Move commands should be 
#  integers in the range of +/-32,767.) If no default is explicitly 
#  defined in the metadata, all symbols are implicitly defined as 0. 
#
#  It is helpful to have a consistent and distinct formatting for
#  symbol names (ex. SOME_SYMBOL1, OTHER_SYM2, etc)
#
# The remaining elements (zero or more) are command objects.
# There are two types of commands:
#   'Remote' - messages to microcontroller (see serial protocol spec)
#   'Local'  - commands affecting program operation
# Commands may be written in compact (array) or verbose (object) form,
# and may reference symbolic values.
# Compact form begins with a name (for flow control), which must be
# unique. The leading character of the actual command is next, with
# upper-case used for remote commands and lower-case for local.
# The remaining elements contain the default arguments.
#   Compact examples:
#     Move one motor:
#       ["move_1","M",1,-100]
#     Move specific motors to same positions:
#       ["move_odd","M",[1,3,5],100]
#     Move all motors to home positions:
#       ["move_all","M","all",0]
#     Move all motors to symbolic position:
#       ["move_all_sym","M","all","POS_HOME"]
# Verbose form, while an object, should still be written in order for
# readability. Required fields are a unique "name", the specific
# "command" (which may be written using the single-character as in 
# compact form, or the full lower-case word), and the named arguments.
#   Verbose examples:
#     {"name":"command":"move","motors":1,"positions":-100}
#
# Not all messages defined in the serial protocol are supported.
# Remote commands:
#   Listed as "compact_character" "verbose_word"
#     "C" "calibrate"
#       "motors" - single integer 1-5, or array of the same.
#     "D" "delay"
#       "motors" - single integer 1-5, or array of the same.
#       "ticks"  - unsigned 16-bit integer, representing the number of
#                  clock cycles of delay (approx 1/48E6 seconds/tick)
#    "light" ("L")
#    "move" ("M") 
#    "pin" ("P")
#    "stop" ("S")
#    "trigger" ("T")
#    "reset" ("R")
#    Special case commands, like clear and done (implemented in the
#    "Light" command), have a shorthand form:
#      "DONE" ("DONE")
#        Turns the indicator LED purple.
#      "CLEAR" ("CLEAR")
#        Removes the Done and Error conditions to return the indicator
#        LED to either green or yellow (calibrated or not).
# Local commands:
#  "w" "wait" - conditions: time, arrive, error val, error code, symbol value (or not), timeout
#      defaults: ["name","w",timeout_seconds,"timeout message"] - waits for all motors to go idle
#  "g" "goto"
#  "p" "prog" - chain execution of programs, or specify self to loop
#  "s" "syms" update symbol(s)
#

#
# Commands may read symbolic values defined in the UI.
#
class ProgramThread(threading.Thread):
    def __init__(self, api, autostep, stepdelay): 
        threading.Thread.__init__(self)
        self.msgi = None
        self.msgo = None
        self.api = api
        self.filename=None
        self.progname=None
        self.program = None
        self.autostep = autostep
        self.stepdelay=stepdelay
        self.running=False

    def load(self,progname,isfile):
        if isfile:
            self.filename='programs/'+progname+'.json'
            self.progname=progname
            try:
                print('attempting to open "programs/'+self.filename+'.json')
                fp = open('programs/'+self.filename+'.json')
                print('attempting to load "programs/'+self.filename+'.json')
                self.program = json.load(fp)
                fp.close()
            except Exception as e:
                self.filename=None
                self.progname=None
                print(str(e))
                return False
        else: #not a file, should be string containing json
            self.filename=None #in case this thread gets reused, clear any lingering details
            self.progname=None
            try:
                print('attempting to load string')
                self.program = json.loads(progname)
            except Exception as e:
                print(str(e))
                return False
        #have valid JSON, examine structure and fields to validate
        if type(self.program) != list:
            print('program must be an array')
            self.program = None
            return False
        if len(self.program) == 0:
            print('program is empty')
            self.program = None
            return False
        if type(self.program[0]) != dict:
            print('first element must be an object')
            self.program = None
            return False
     #   for

    def do_cmd(self,cmd):
        pass

    def run(self):
#        if not self.program:
#            print('no program loaded')
#            return
#        if 'description' in self.program.keys():
#            print(self.program['description'])
#        cmdn=0
#        for cmd in self.program:
#            #check queue for control messages
#            if type(cmd) == list: #array (compact) form
#                pass
#            elif type(cmd) == dict: #object (verbose) form
#                pass
#            else: #program was tampered with
#                print('program command must be given as array or object ('+str)
#            cmdn += 1
#            if self.stepdelay:
#                time.sleep(stepdelay)
        count = 5
        while count:
            print(count)
            self.api.send([b'M','1',0,10])
            count -= 1
            time.sleep(0.5)


# NOT IMPLEMENTED - threaded communication implementation
# Intended to be standalone relay between process and micro via serial
class SerialThread():
    pass


# Intended to be standalone relay between process and MATLAB via socket
class SocketThread():
    pass


# Implementation of persistent communication relay and state storage
# Connected to/instantiated by API.connect()
# MATLAB sends to process via API.send() which uses a socket
# process/sequencer sends to MATLAB via engine connection
# state info: which motors calibrated since reset/poweron, motor positions (by encoder and by sent steps)
# and delays (given by A and D cmds, 
# light state/values, done/error, pin states, 
class CommProcess:
    def __init__(self,serial_port=SERIAL_PORT_DEFAULT,socket_port=SOCKET_PORT_DEFAULT):
        #comm object device names/addresses
        self.socket_port=socket_port
        self.serial_port=serial_port

        #comm object handles
        self.mc_com = None      #serial port object
        self.listen_sock = None #connection source socket
        self.api_sock = None    #active data socket
        self.matlab=None        #matlab engine instance
        #self.matlab.workspace['proc']=1

        #for async implementation - not currently used
        self.rsocks = []
        self.wsocks = []
        self.xsocks = []
        
        #in/outbound message queues to/from various nodes (mc: microcontroller, ui: MATLAB)
        self.mc_omsg = [] #microcontroller outbound
        self.mc_imsg = [] # " " " inbound
        self.ui_omsg = [] #matlab inbound
        self.ui_imsg = [] # "  " outbound
        self.pt_omsg = queue.Queue() #program thread inbound
        self.pt_imsg = queue.Queue() # " " out

        #internal control vars
        self.done = False #main loop exit condition
        self.cl_delay = 0 #connection retry delay counters (seconds)
        self.sv_delay = 0
        self.mc_delay = 0
        self.api_delay = 0 #how long to wait to read indicated number of bytes (or at least four) before resetting

        self.logfile=open('rfis_proc.log','at')
        self.print("\nrfis.CommProcess.__init__\n")

    def __del__(self):
        self.print('rfis.CommProcess.__del__')
        if self.matlab:
                try:
                    self.matlab.exit()
                except Exception as e:
                    self.print(str(e))
                    self.matlab = None
        if self.listen_sock:
            self.listen_sock.close()
            self.listen_sock = None
        if self.api_sock:
            self.api_sock.close()
            self.api_sock = None
        if self.mc_com:
            self.mc_com.close()
            self.mc_com = None
        if self.logfile: #todo: is this needed?
            self.logfile.flush()
            self.logfile.close()
        time.sleep(10)

    def print(self,msg,end="\n"):
        print(msg,end=end)
        if self.logfile:
            self.logfile.write(msg+end)
        if self.matlab:
            try:
                self.matlab.RFIS_notify(MSG_TYPE_COM,'{"type":"log","message":"'+msg+'"}'+end)
            except:
                pass

    def open_serial(self):
        pass

    def close_serial(self):
        pass

    def open_socket(self):
        pass

    def close_socket(self):
        pass

    def open_matlab(self):
        pass

    def close_matlab(self):
        pass

    def run(self):
        self.print('rfis.CommProcess.run: attempting to open serial port')
        try:
            self.mc_com=serial.Serial(self.serial_port,SERIAL_BAUD_RATE)
        except:
            self.print('exception opening serial port \"%s\"' % self.serial_port)
            self.mc_com = None
        self.print('rfis.CommProcess.run: attempting to create listen socket')
        self.listen_sock = socket.socket()
        self.listen_sock.settimeout(0.2)
        self.listen_sock.bind(('localhost', self.socket_port))
        self.listen_sock.listen()
        self.print('rfis.CommProcess.run: attempting to connect to MATLAB')
        try:
            import matlab.engine #was getting an error when this was a global import (b/c nesting in matlab via api?)
            #^seems to work ok here as this is only called in separate process
            self.matlab = matlab.engine.connect_matlab(MATLAB_NAME)
        except Exception as e:
            self.print('rfis.CommProcess.run: failed to connect to MATLAB'+str(e))
            return
        self.matlab.RFIS_notify(MSG_TYPE_COM,'{"type":"log","message":"Well, here we are."}') #TODO: use api to build messages

        api_msg_size=0 #first four bytes of any message are size, big endian
        api_msg_data=bytes() #current message

        t_delta = 0
        t_start = 0
        t_end = time.time()
        self.print('rfis.CommProcess.run: main loop')
        while not self.done: #pump messages over every channel
            t_start = t_end
            #print('t_delta: '+str(t_delta))
            if self.serial_port:
                if self.mc_com: #open serial?
                    if self.mc_com.inWaiting() >= 4:
                        self.print('MC->',end="")
                        self.ui_omsg.append(self.mc_com.read(4)) #add a message to be sent to UI
                        self.print(' '.join("{:02X}".format(c) for c in self.ui_omsg[-1]))#str(self.ui_omsg[-1]))
                    if len(self.mc_omsg):
                        self.print('MC<-'+' '.join("{:02X}".format(c) for c in self.mc_omsg[0]))#)str(self.mc_omsg[0]))
                        self.mc_com.write(self.mc_omsg.pop(0)) #post next message
                else: #not open, but valid portname
                    if self.mc_delay > 2:
                        self.print('Serial: re-open')
                        try:
                            self.mc_com=serial.Serial(self.serial_port)
                        except:
                            self.mc_com = None
                            #TODO: check to see that com port is still valid
                        self.mc_delay = 0
                    else:
                        self.mc_delay += t_delta
            if self.listen_sock: #can we try to re-accept?
                if not self.api_sock or self.api_sock._closed: #do we need to re-accept?
                    if self.cl_delay > 2:
                        self.print('Socket: re-accept')
                        try:
                            self.api_sock, addr = self.listen_sock.accept()
                            self.api_sock.settimeout(0) #WAS .2 - try 0?
                        except OSError as e:
                            self.api_sock = None
                        self.cl_delay = 0
                    else:
                        self.cl_delay += t_delta
                else: #have an API client
                    recv_ok = True
                    d = []
                    try:
                        d=self.api_sock.recv(4096) #get a bunch of bytes (is a larger buffer size better?)
                    except socket.timeout as to:
                        recv_ok = False
                        pass #TODO: what goes here?
                    except OSError as e:
                        #self.print('Exception on read attempt: '+str(e))
                        recv_ok = False
                    #assume we have data (might have leftovers, so can't just check on reception of new bytes)
                    #if len(api_msg_data):
                    #    self.print('SIZE:'+str(api_msg_size)+' LEN:'+str(len(api_msg_data)))
                    if not api_msg_size and len(api_msg_data) >= 4: #size not read yet, but have enough bytes to do it
#                        self.print("NOT DEFINED")
#                        self.print(str(type(api_msg_data)))
#                        self.print(str(api_msg_data))
                        api_msg_size=int.from_bytes(api_msg_data[0:4],'big')
#                        self.print("DEFINED")
                        api_msg_data = api_msg_data[4:] #keep only the data now
#                        self.print('expecting '+str(api_msg_size)+' bytes')
                        if len(api_msg_data) >= api_msg_size: #have a full message
                            if api_msg_size == 4: #keep this special case for serial packets
                                p=api_msg_data
                                self.print('UI->'+' '.join("{:02X}".format(c) for c in p[0:4]))
                                if chr(p[0]) in['C','D','L','M','P','R','S']: # got a message
                                    self.mc_omsg.append(p[0:4])
                                    api_msg_data=api_msg_data[4:] #lop off the data we just handled
                                    api_msg_size = 0 #ready to process next bit of data
                                elif p[0] == ord('X'):
                                    self.print('UI: got SHUTDOWN')
                                    self.done = True
                                    api_msg_data = 0
                                    api_msg_size = 0
                                else:
                                    self.print('WTF: '+str(p)) #ignore garbage
                                    api_msg_data=api_msg_data[4:]
                                    api_msg_size = 0
                                #self.print('p[0]: '+str(p[0]))
                                #if len(self.ui_omsg):
                                #    self.print('UI<-'+' '.join("{:02X}".format(c) for c in self.ui_omsg[0]))#+str(self.ui_omsg[0]))
                                #        self.api_sock.send(self.ui_omsg.pop(0))
                            else:
                                self.print('WHY AM I JSONING THIS: '+str(api_msg_data),str(api_msg_size))
                                try:
                                    self.print(str(api_msg_data[0:api_msg_size]))
                                    jd = json.loads(api_msg_data[0:api_msg_size])
                                except Exception as e:
                                    self.print('Failed to decode JSON message')
                                if jd['type'] == MSG_TYPE_SEQ:
                                    pass
                    if len(d): #got some bytes
                        self.print('GOT BYTES: '+str(len(d))+' '+str(type(d)))
                        api_msg_data += d #add to buffer
                        self.print('HERE THEY ARE: '+str(api_msg_data)+'\n\t'+str(type(api_msg_data))+' '+str(len(api_msg_data)))
                    elif recv_ok: #TODO: check for valid shutdown? or assume crash of some kind?
                        self.print('UI: disconnected (0-length recv)')
                        self.api_sock.shutdown(socket.SHUT_RDWR)
                        self.api_sock.close()
                        self.api_sock = None
                        api_msg_size = 0
                        api_msg_data = bytes()
                            
                    
#                    d=[]
#                    read=True #no exception on read
#                    try:
#                        #print('UI: try read')
#                        d=self.api_sock.recv(4)
#                    except socket.timeout as to:
#                        #self.print('Timeout on read attempt')
#                        read = False
#                    except OSError as e:
#                        self.print('Exception on read attempt'+str(dir(e))+"\n\t"+str(e.args)+' '+str(type(e.characters_written))+' '+str(e.errno)+' '+str(e.filename)+' '+str(e.filename)+' '+str(e.strerror)+' '+str(e.winerror)+' '+str(e.with_traceback)) #str(e.errno)+" "+str(e.strerror))
#                        read = False
#                    if len(d): #TODO: we're assuming we got 4 bytes - fix
#                        self.print('UI->'+' '.join("{:02X}".format(c) for c in d))
#                        if d[0] is ord('C') or d[0] is ord('L') or d[0] is ord('M') or d[0] is ord('P') or d[0] is ord('S'):
#                        self.mc_omsg.append(d)
#                        if d[0] == ord('X'):
#                            self.print('UI: got SHUTDOWN')
#                            self.done = True
#                        else:
#                            print('WTF: '+d) #ignore garbage
#                        self.print('d[0]: '+str(d[0]))
#                        if len(self.ui_omsg):
#                            self.print('UI<-'+' '.join("{:02X}".format(c) for c in self.ui_omsg[0]))#+str(self.ui_omsg[0]))
#                            self.api_sock.send(self.ui_omsg.pop(0))
#                    elif read: #0 bytes
#                        self.print('UI: disconnected (0-length recv)')
#                        self.api_sock.shutdown(socket.SHUT_RDWR)
#                        self.api_sock.close()
#                        self.api_sock = None
            else: #no listening socket
                if self.sv_delay > 2:
                    self.print('Socket: re-create')
                    try:
                        self.listen_sock = socket.socket()
                        self.listen_sock.settimeout(0.2)
                        self.listen_sock.bind(('localhost',self.socket_port))
                        self.listen_sock.listen()
                    except OSError as e:
                        self.list_sock = None
                    self.sv_delay = 0
                else:
                    self.sv_delay += t_delta
            t_end = time.time()
            t_delta=t_end-t_start

    #TODO: asynchronous implementation using threads
    def runX(self):
        self.logfile.write("CommProcess.run\n")
        self.mc_com=serial.Serial(self.serial_port)
        self.listen_sock = socket.socket()
        self.listen_sock.bind(('localhost', self.socket_port))
        #self.listen_sock.setblocking(False)
        self.listen_sock.listen() #TODO: leave blank when we can guarantee version >= 3.5
        self.rsocks = [self.listen_sock]
        self.wsocks = [self.listen_sock]
        self.xsocks = [self.listen_sock]
        while not self.done:
            if self.mc_com:
                if self.mc_com.inWaiting() != 0:
                    msg=self.mc_com.read(4)
    #        pass # handle serial messages from MC
            [rs, ws, xs] = select.select(self.rsocks,self.wsocks,self.xsocks)
            for s in rs: #handle all socket reads
                if s is self.listen_sock: #new connection
                    if self.api_sock is None: #only accept if we don't already have a connection?
                        self.api_sock, addr = s.accept()
                        self.rsocks.append(self.api_sock)
                        self.wsocks.append(self.api_sock)
                        self.xsocks.append(self.api_sock)
                        self.logfile.write("CommProcess.run: client connected\n")
                else: #only other possibility is the matlab/api connection
                    msg = s.recv(1024)
                    if msg:
                        self.logfile.write("CommProcess.run: received data: \""+msg+"\"\n")
                        if msg[0] is 'C' or msg[0] is 'L' or msg[0] is 'M' or msg[0] is 'P' or msg[0] is 'S':
                            self.mc_com.write(msg) #forward packet to MC
                        else: #TODO: check for more process-specific messages
                            if msg[0] is 'X':
                                self.logfile.write("CommProcess.run: shutting down...n")
                                self.done = True
                                #continue? TODO
                            else:
                                self.logfile.write("\t(invalid)\n")
                    else: #empty message indicates closed connnection
                        self.logfile.write("CommProcess.run: client closed connection\n")
                        if s in self.rsocks:
                            self.rsocks.remove(s)
                        if s in self.wsocks:
                            self.wsocks.remove(s)
                        if s in self.xsocks:
                            self.xsocks.remove(s)
                        self.api_sock.close()
                        self.api_sock = None
            for s in ws: #handle all socket writes
                pass
            for s in xs: #handle all socket exceptions (on windows/winsock: connect failed (N/A for this, a server) or OOB data available
                pass
                    
        #send serial to MC
        #send socket to MATLAB


# API implementation
# This class implements methods for communication between MATLAB and 
# CommProcess. It does NOT maintain system state.
# Method functionality:
#   enumerating serial ports
#   building serial messages (low-level)
#   building JSON messages for higher-level control and sequencing
#   spawning a CommProcess and connecting to it
#   sending messages (all types) via socket to the process
class API:
    def __init__(self,serial_port=SERIAL_PORT_DEFAULT,socket_port=SOCKET_PORT_DEFAULT):
        self.serial_port=serial_port
        self.socket_port=socket_port
        self.serial_ports=[]
        self.socket=None #value indicates connection state (None = no connection)
        self.process=None
        self.send_lock=False
        #WHY SOCKETS: how to re-acquire stdin/stdout of process if matlab crashes? easy to get channel if a socket

    def __del__(self):
        print('rfis.API.__del__')
        self.shutdown(True)

    def detect_serial_ports(self):
        ports = list_ports.comports()
        for port, desc, addr in ports:
            self.serial_ports.append((port,desc,addr))
        return self.serial_ports

    #NOTE: do not call connect if this api instance isn't in MATLAB
    def connect(self,serial_port=None,socket_port=None):
        if serial_port is not None:
            self.serial_port = serial_port
        if socket_port is not None:
            self.socket_port = socket_port
        #TODO: see if process is running by checking process list/args
        #attempt connection to process:
        if not self.socket:
            print("1 rfis.API.connect: no socket")
            try:
                self.socket=socket.socket()
            except OSError as e:
                self.socket = None
                print("2 rfis.API.connect: failed to create socket: "+e.strerror)
                return False
            self.socket.settimeout(0.2)
        try:
            print("3 "+str(self.socket_port))
            self.socket.connect(('localhost',self.socket_port))
        except socket.timeout as e:
            print("3 rfis.API.connect: connect timed out")
        except OSError as e:
            print("3 rfis.API.connect: failed to connect to communications process: "+e.strerror)
            self.socket.close()
            self.socket=None
        else:
            return True
        if not self.process:
            #not running or can't connect, start new process
            print("4 rfis.API.connect: attempting to launch new process")
            self.process=subprocess.Popen(["python", "-m", "rfis", ">", "debug.txt"])#),shell=True)#,stdout=subprocess.PIPE)
            if not self.process:
                print("5 rfis.API.connect: failed to open process")
                self.process=None
                return False
        if not self.socket:
            print("6 rfis.API.connect: no socket")
            try:
                self.socket=socket.socket()
                #self.socket.setblocking(False)
            except OSError as e:
                self.socket = None
                print("7 rfis.API.connect: failed to create socket: "+e.strerror)
                return False
            self.socket.settimeout(0.2)
        try:
            print("8 "+str(self.socket_port))
            self.socket.connect(('localhost',self.socket_port))
        except OSError as e:
            print("8 rfis.API.connect: failed to connect to communications process: "+e.strerror)
        print("9 rfis.API.connect: success")
        return True

    #DO NOT USE
    def status(self):
        if self.socket:
            msg=self.socket.read()

    #DO NOT USE
    def read_next(self):
        if self.socket:
            d=[]
            try:
                d=self.socket.recv(4)
            except OSError as e:
                pass
            if len(d) == 4:
                return d
        return b'    '

    #DO NOT USE
    def read_nextX(self):
        if self.socket: #see if we can get any new stuff first
            msg=[]
            try:
                msg=self.socket.recv(1024) #get as many bytes as we can
            except OSError as e:
                if e.errno is 10035: #non-blocking wasn't ready
                    pass
                else:
                    pass
            print('rfis.API.read_next: got '+len(msg)+' bytes')
            mi=0 #message byte index
            while mi < len(msg): #go through each byte of msg
                while len(self.ibuf) < 4 and mi < len(msg): #build 4-byte message
                    self.ibuf += msg[mi] #grab bytes from message
                    mi += 1
                if len(self.ibuf) is 4: #got 4 bytes TODO: check for proper formatting
                    print('rfis.API.read_next: received message: '+self.ibuf[len(ibuf)-1])
                    self.msgs.append(self.ibuf)
                    self.ibuf=[]
        if len(self.msgs):
            return self.msgs.pop(0)
        return None

    def query(self):
        pass #get state

    def shutdown(self,safe=None):
        print('API shutdown')
        if self.socket:
            #TODO: check for safe shutdown vs crash (only send shutdown to process if GUI shuts down cleanly)
            print('\tsending shutdown and closing socket')
            self.socket.send(b'{"type":"message","message":"shutdown"}')
            time.sleep(1) #give it a second to think
            self.socket.shutdown(socket.SHUT_RDWR)
            self.socket.close()
            self.socket=None

# Message filters - --  -- -  - 
    def msg_calibrate(self,motor):
        if self.prog_thread:
            print('rfis.API.calibrate: blocked by active program')
            return False
        if ord(motor) < ord('1') or ord(motor) > ord('5'):
            print('rfis.API.calibrate: invalid motor number')
            return False
#        return ([b'C',motor,b'\x00',b'\x00'])
        return b'["C"]'

    def msg_delay(self,motor,delay):
        if self.prog_thread:
            print('rfis.API.calibrate: blocked by active program')
            return False
        if ord(motor) < ord('1') or ord(motor) > ord('5'):
            print('rfis.API.delay: invalid motor number')
            return False
#        return self.send([b'D',motor,(delay>>8)&0xff,delay&0xff])
        return b'["M"]'

    # lightnum 0-16, rgb tuple of 3 bytes
    def msg_light(self,lightnum,rgb):
        if self.prog_thread:
            print('rfis.API.calibrate: blocked by active program')
            return False
        if lightnum < 0 or lightnum > 16:
            print('rfis.API.light: invalid light number')
            return False
        print((bytearray([ord('L'), (lightnum<<2)&0xfc|((rgb[0]>>6)&3), (rgb[2]>>2)&0xff, rgb[2]&0xcf])))
        return self.send((bytearray([ord('L'), (lightnum<<2)&0xfc|((rgb[0]>>6)&3), (rgb[2]>>2)&0xff, rgb[2]&0xcf]))) #TODO: review

    def msg_move(self,motor,steps):
        if self.prog_thread:
            print('rfis.API.calibrate: blocked by active program')
            return False
        if motor < '1' or motor > '5':
            print('rfis.API.move: invalid motor number')
            return False
        if steps > 0:
            sign=0
        else:
            sign=0x80
            steps = -steps
        return self.send([b'M', motor, ((steps>>8)&0xff)|sign, steps&0xff])#b'M'+bytes(motor,'utf-8')+((steps>>8)&0xff00).to_bytes(1))

    def msg_pin(self,pin,state):
        if self.prog_thread:
            print('rfis.API.calibrate: blocked by active program')
            return False
        if pin < '0' or pin > '2':
            print('rfis.API.pin: invalid function number')
            return False
        #TODO: validate pin and state
#        return self.send([b'P',pinbytes(state,'utf-8'),b'\x00'])
        return [b"P",pin&0xff]

    def msg_reset(self):
        if self.prog_thread:
            print('rfis.API.calibrate: blocked by active program')
            return False
        #return self.send(bytes([b'R',b'\x00',b'\x00',b'\x00']))
        return b'["R"]'

    def msg_stop(self):
        if self.prog_thread:
            print('rfis.API.calibrate: blocked by active program')
            return False
        #return bytes([b'S',b'\x00',b'\x00',b'\x00'])
        return b'["S"]'

# End message filters - - - - - - 

    def send(self,rawmsg): #sends string of bytes, no 4-byte enforcement (just checks that bytes are sent by comparing rawmsg list size to num bytes sent by socket.send)
        #This is all really gross, but this is what i get for trying to redesign internal communications at 4:42am the morning of design day
        print('rfis.API.send: '+str(rawmsg))
        msglen = len(rawmsg)&0xffffffff
        print('MSGLEN: '+str(msglen))
        msg=[(msglen>>24)&0xff,(msglen>>16)&0xff,(msglen>>8)&0xff,msglen&0xff]
        print('LEN: '+str(msg))
        msg.append(rawmsg)
        print('LEN+MSG: '+str(msg))
        outmsg=[]
        for ii in range(len(msg)):
            if type(msg[ii]) == str or type(msg[ii]) == bytes:
                for jj in range(len(msg[ii])):
                    print(msg[ii][jj])
                    outmsg.append(ord(msg[ii][jj]))
            else:
                outmsg.append(msg[ii])
        print('OUTMSG: '+str(outmsg))
        msg=outmsg
        if self.socket:
            print('rfis.API.send: attempting')
            try:
                n=self.socket.send(bytearray(msg))
                if n is not len(msg):
                    print('rfis.API.send: sent only '+n+' of '+str(len(msg))+' bytes')
                    return False
                #n=self.socket.send(ord(rawmsg[0]).to_bytes(1,sys.byteorder)+ord(rawmsg[1]).to_bytes(1,sys.byteorder)+ord(rawmsg[2]).to_bytes(1,sys.byteorder)+ord(rawmsg[3]).to_bytes(1,sys.byteorder))#bytes(rawmsg[0],'ascii')+bytes(rawmsg[1],'ascii')+bytes(rawmsg[2],'ascii')+bytes(rawmsg[3],'ascii'))
#                if n is not len(rawmsg):
#                    print(type(rawmsg),type(n))
#                    print('rfis.API.send: sent only '+n+' of '+str(len(rawmsg))+'bytes')
#                    return False
                print('rfis.API.send: success')
                return True
            except Exception as e:
                print('rfis.API.send: exception: '+str(e))
                return False
        print('rfis.API.send: no socket')
        return False

    #sends json message containing symbols and their values to be defined/updated
    def set_symbols(self,symbols):
        #create message
        if self.socket:
            try:
                n=self.socket.send(symbols)
                return True
            except:
                pass
                return False

    #sends json message requesting specified symbol defs to comm process
    def get_symbols(self,symbols):
        #create message {"type"
        if self.socket:
            try:
                n=self.socket.send(symbols)
                return True
            except:
                pass
                return False

    #upload a string (or specify a file) for the process to execute as a sequencing program
    #prog - filename or json string
    #isfile - boolean indicating whether prog represents a file name or json
    #override - abort any running program
    #returns sending success (nothing about validity of program)
    def do_program(self,prog,isfile,override): #spawns a thread on the process to execute the given action sequence, blocking other UI->MC commands
        print('do_program: '+str(prog))
        if isfile:
            return False # not implemented yet
        else:
            wrap = [ord('['),ord('{'),ord('}'),ord(',')]
            for ii in range(len(prog)):
                wrap.append(ord(prog[ii]))
            wrap.append(ord(']'))
            print('RESULT: '+str(wrap))
            bytearray(wrap)
        if self.socket:
            try:
                n=self.socket.send(bytearray(wrap))
            except Exception as e:
                print('rfis.API.do_program: failed to send program: '+str(e))
                return False
        return True
        #purge incoming messages before starting (after every command, listen for response?)
        #passing None (null) in prog stops active program
        #if self.prog_thread:
        #    if self.prog_thread.running:
        #        if not override: #program not active?
        #            print('rfis.API.do_program: blocked by active program with override disabled')
        #            return False
        #        else:
        #            pass
        #            #stop program
        #else:
        #    self.prog_thread = ProgramThread()
        #ret=self.prog_thread.load(prog,isfile)
        #if not ret(0):
        #    print('rfis.API.do_program: failed to load \"'+prog+'.json')
        #    return ret
        #try:
        #    self.prog_thread.start()
        #except:
        #    return False
        #return True




#api runs this module as a script (in api.connect()) to start the comms process
if __name__ == '__main__':
    CommProcess().run()
