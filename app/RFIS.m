function varargout = RFIS(varargin)
% RFIS callbacks and application code for corresponding UI figure
%
%   RFIS implements the MATLAB side of the Robatic Foram Imaging System
%   application, developed for ECE Senior Design at NC State (2017-18).
%
%   RFIS, by itself, creates a new RFIS or raises the existing
%   singleton*.
%
%   H = RFIS returns the handle to a new RFIS or the handle to
%   the existing singleton*.
%
%   RFIS('CALLBACK',hObject,eventData,handles,...) calls the local
%   function named CALLBACK in RFIS.M with the given input arguments.
%
%   RFIS('Property','Value',...) creates a new RFIS or raises the
%   existing singleton*.  Starting from the left, property value pairs are
%   applied to the GUI before RFIS_OpeningFcn gets called.  An
%   unrecognized property name or invalid value makes property application
%   stop.  All inputs are passed to RFIS_OpeningFcn via varargin.
%
%   *See GUI Options on GUIDE's Tools menu.  Choose "GUI allows only one
%   instance to run (singleton)".
%
%   Dependencies:
%     MATLAB:
%       Scripts:
%         RFIS.m
%         RFIS_notify.m
%         imbinarize.m
%         otsuthresh.m
%       Add-ons:
%         USB Webcam Support with MATLAB
%     Python:
%       3.5.4
%       pySerial
%       MATLAB Engine for Python
%
%   Author: Eric Davis (ecdavis@ncsu.edu)
%
% See also: GUIDE, GUIDATA, GUIHANDLES

% Begin initialization code - DO NOT EDIT
gui_Singleton = 1;
gui_State = struct('gui_Name',       mfilename, ...
                   'gui_Singleton',  gui_Singleton, ...
                   'gui_OpeningFcn', @RFIS_OpeningFcn, ...
                   'gui_OutputFcn',  @RFIS_OutputFcn, ...
                   'gui_LayoutFcn',  [] , ...
                   'gui_Callback',   []);
if nargin && ischar(varargin{1})
    gui_State.gui_Callback = str2func(varargin{1});
end

if nargout
    [varargout{1:nargout}] = gui_mainfcn(gui_State, varargin{:});
else
    gui_mainfcn(gui_State, varargin{:});
end

%% RFIS-SPECIFIC INITIALIZATION
%    disp 'Clearing classes...'
    %clear classes; %for updating w/ changes in rfis.py (TODO: can we remove in final version, or do we expect user to make changes?)
    clear py.rfis
    mod=py.importlib.import_module('rfis');
    py.importlib.reload(mod);
%    disp 'Done clearing classes'
% End initialization code - DO NOT EDIT


% --- Executes just before RFIS is made visible.
function RFIS_OpeningFcn(hObject, eventdata, handles, varargin)
% This function has no output args, see OutputFcn.
% hObject    handle to figure
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)
% varargin   command line arguments to RFIS (see VARARGIN)

% Choose default command line output for RFIS
  handles.output = hObject;

% enable sharing w/ Python
  if ~matlab.engine.isEngineShared
    try
      matlab.engine.shareEngine('RFIS')
    catch E
      disp 'Unable to share engine';
    end
  end

% open the API
  handles.api=py.rfis.API();
  if ~handles.api.connect()
    disp 'API connection failed at startup'
  end

% create socket object (port may be redefined by user later
  handles.socket=tcpip('localhost',py.rfis.SOCKET_PORT_DEFAULT*1); %the *1 is a hack - fails if you pass a py.int directly (have to convert)

% Update handles structure
  guidata(hObject, handles);

% UIWAIT makes RFIS wait for user response (see UIRESUME)
% uiwait(handles.RFIS);


% --- Outputs from this function are returned to the command line.
function varargout = RFIS_OutputFcn(hObject, eventdata, handles) 
% varargout  cell array for returning output args (see VARARGOUT);
% hObject    handle to figure
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Get default command line output from handles structure
varargout{1} = handles.output;




%%%%%%%% GENERAL FUNCTIONS (non-callback)

%searches handles (list) for all objects with tag matching str
%returns list of indicies
function indices = findInHandles(handles,str)
  indices = [];
  for ii=1:length(handles) %find all camera list popup menus
    if count(handles(ii).Tag,str)
        indices=[indices,ii];
    end
  end


%%%%%%%% DEFAULT CALLBACKS (for stuff like setting edit widgets' default appearance, etc.)

% --- Executes during object creation, after setting all properties.
function edit_default_CreateFcn(hObject, eventdata, handles)
% hObject    handle to edit (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: edit controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end


% --- Executes during object creation, after setting all properties.
function slider_default_CreateFcn(hObject, eventdata, handles)
% hObject    handle to slider35 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: slider controls usually have a light gray background.
if isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor',[.9 .9 .9]);
end


% --- Executes during object creation, after setting all properties.
function popupmenu_default_CreateFcn(hObject, eventdata, handles)
% hObject    handle to popupmenu6 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: popupmenu controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end



%%%%%%%%% COMMON CALLBACKS (helpers and multiple instance functionality, like screen switching, etc.)

% switches visibility of panels
% assumes Parent of hObject is the panel to be hidden, and
% hObject.UserData.Target is the Tag of the panel to show
% hObject.Parent.UserData may also contain OnEnter and OnLeave,
% which are names of functions to call when the panel is shown and hidden
function btnSwitchScreen(hObject, eventdata, handles)
%    disp hObject.Tag
%disp Parent:
%disp(hObject.Parent)
%disp UserData:
%disp(hObject.UserData)
%disp Target:
%disp(handles.(hObject.UserData.Target))
%hObject.Parent.Tag;
  oldpnl = hObject.Parent;
  oldpnl.Visible='off';
  if isfield(oldpnl.UserData,'OnLeave')
    disp('OnLeave defined')
    %fh=str2func(oldpnl.OnLeave);
    %fh();
    eval(oldpnl.UserData.OnLeave);
  end
%end

  newpnl = handles.(hObject.UserData.Target);
  newpnl.Visible='on';
  if isfield(newpnl.UserData,'OnEnter')
    disp('OnEnter defined')
    %fh=str2func(newpnl.OnEnter);
    %fh();
    eval(newpnl.UserData.OnEnter);
  end
%    handles.(hObject.UserData.Target).Tag
%    guidata(hObject,handles);
  drawnow;


function btnZeroAllMotors(hObject, eventdata, handles)
  ret = 0;
  for ii=49:53 %ascii '1' to '5'
    ret=handles.api.send(char([67,ii,0,0])); %do all motors
  end

  
function btnReset(hObject, eventdata, handles)
  handles.api.send(char([82,0,0,0]));
  
  
function btnMotorDelay(hObject, eventdata, handles)
  %read targets TODO input validation
  h=findobj('Tag',strcat(strcat('D',hObject.Tag(6)),'TargetDelay'));
  delay = uint16(str2num(h.String));

  %check against limits
%  sign = 0;
%  if steps < 0
%      sign=128; %high bit in single byte
%      steps = -steps;
%  end
%  steps = uint16(steps);
%  hi = bitor(uint16(sign),bitshift(bitand(steps,uint16(hex2dec('ff00'))),-8))
%  lo = uint8(steps)
  %format message
  %send command
  %handles.api.send(char([77,hObject.Tag(5),hi,lo])); %'M' is for move
  handles.api.send(char([68,hObject.Tag(6),uint8(bitshift(bitand(delay,uint16(hex2dec('ff00'))),-8)),uint8(delay)])); %'M' is for move
    %handles.api.send(char([77,'2',hi,lo])); %'M' is for move
    %  handles.api.send(char([77,'1',hi,lo])); %'M' is for move
    %    handles.api.send(char([77,'1',hi,lo])); %'M' is for move
        
  %handles.api.do_move(hObject.Tag(5),steps);


function btnRunProgram(hObject, eventdata, handles)
  %get selected program
  %hObject.UserData
  %h=findobj('Tag',hObject.UserData.Source); %find source program list
  %progname = h.String{h.Value};
  %stop any active program
  %TODO
  %start program
  
  %FOR DEMO:
  %load string from txeProgram_Setup4
  h=findobj('Tag','txeProgram_Setup4');
  if handles.api.do_program(strjoin(h.String),false,false)
      disp 'SUCCESS';
  end

function txeTargetSteps(hObject, eventdata, handles)

function txeTargetMM(hObject, eventdata, handles)

function txeLimit(hObject, eventdata, handles)

function txeMotorStepDelay(hObject, eventdata, handles)
%%%%%%%% SPECIFIC CALLBACKS (for single-instance functionality - should not be many here, as functions generally occur on multiple screens)

% communications



function updateImage(hObject, eventdata, handles)
  %proceed if visible axes (can we check axes, or do we have to check parent?)
  %proceed if active camera
  if ~exist('handles.cam') || ~isvalid(handles.cam)
    return
  end
  axes(handles.camAxes);
  %get time for fps
  handles.img=snapshot(handles.cam); %get frame
  if ~handles.detector %no detection enabled, all done
    return
  end
  %calc fps w/o detection
  %run foram detection
  %update image

%% screen transition pseudo-callbacks (invoked explicitly, not activated by matlab)

function enterSetup3()
disp('enterSetup3')
  %activate camera callback if camera is active
  handles=guidata(gcf);
  handles.camAxes=findobj('Tag','CamAxes_Setup3');
  guidata(gcf,handles);

function leaveSetup3()
disp('leaveSetup3')
  %disable any active camera callback if camera is active
  handles=guidata(gcf);
  handles.camAxes=0;
  guidata(gcf,handles);

function enterSetup4()

function leaveSetup4()





function btnSendBytes(hObject, eventdata, handles)
  %todo: validate input from editRaw
  h=findobj('Tag','editRaw');
  if ishandle(h)
      s = strsplit(h.String);
      if length(s) == 4
          d = zeros(1,length(s));
          d(1) = sscanf(char(s(1)),'%x');
          d(2) = sscanf(char(s(2)),'%x');
          d(3) = sscanf(char(s(3)),'%x');
          d(4) = sscanf(char(s(4)),'%x');
        if isfield(handles,'api')% && ishandle(handles.api) %try to send
          disp 'SENDING BYTES'
          data = char([d(1),d(2),d(3),d(4)])
          handles.api.send(data);
        end
      end
  end

  
function btnSetLimit(hObject, eventdata, handles)




function btnStop(hObject, eventdata, handles)
  handles.api.send(char([83,0,0,0])); %send stop command

function btnStopAll(hObject, eventdata, handles)
  handles.api.send(char([83,1,0,0])); %stop (motors)
  handles.api.send(char([80,1,0,0])); %blower off
  handles.api.send(char([80,0,0,0])); %suction off

  


function btnClear(hObject, eventdata, handles)
  handles.api.send(char([76,0,0,0])); %special light command to clear status

function btnDone(hObject, eventdata, handles)
  handles.api.send(char([76,1,0,0])); %special light command to indicate completion of imaging or sorting





% --- Executes when selected object is changed in btgEndEffector.
function btgEndEffector(hObject, eventdata, handles)
  switch get(eventdata.NewValue,'Tag') % Get Tag of selected object.
    case 'pumpPos'
      display('pos');
      handles.api.send(char([80,0,0,0])); %suction off
      handles.api.send(char([80,1,1,0])); %blower on
    case 'pumpNeg'
      display('neg');
      handles.api.send(char([80,1,0,0])); %blower off
      handles.api.send(char([80,0,1,0])); %suction on
    case 'pumpOff'
      display('off');
      handles.api.send(char([80,1,0,0])); %blower off
      handles.api.send(char([80,0,0,0])); %suction off
  end

% --- for individual ring LED selection checkboxes, and also for
% clear/select all buttons
function ckbLEDToggle(hObject,eventdata,handles)
%   led=extractAfter(hObject.Tag,'LED');
%   led = uint8(str2double(led));
%   if     led == 0 %turn off all
%     for ii = 1:16
%       %handles.api.send(char([76,bitshift(ii,2),0,0]));
%       %select all
%     end
%   elseif led == 17 %turn on all
%     for ii = 1:16
%       handles.api.send(char([76,bitshift(ii,2)+3,255,255]));
%     end
%   else %switch one
%     if hObject.Value %on
%       handles.api.send(char([76,bitshift(led,2)+3,255,255]));
%     else %off
%       handles.api.send(char([76,bitshift(led,2),0,0]));
%     end
%   end

function btnLEDColorPicker(hObject,eventdata,handles)
  pnltag=strsplit(hObject.Tag,'_');
  pnltag=pnltag(2);
  pnltag=char(strcat('LEDColor_',pnltag));
  p=findobj('Tag',pnltag); %get associated color display panel
  p.BackgroundColor=uisetcolor([0 0 0]); %TODO: default is last set LED color
  

function btnSetLEDColors(hObject,eventdata,handles)
%  hleds=[];
  hcp=findobj('Tag','LEDColor_Setup4');
  color=hcp.BackgroundColor;
  red1=uint8(bitand(color(1),3)); %buttom two bits in msg start
  red2=uint8(bitshift(bitand(color(1),hex2dec('f')),-4)); %lo for red bits are hi 4 packed
%  grn2=uint8(bitshift(
%  grn3=
  blu=bitshift(color(3),-2);
  for ii=1:16 %get all selected LEDs
    hcb=findobj('Tag',strcat('LED',num2str(ii)));
    if hcb.Value
%      hleds=[hcb hleds];
      handles.api.send(char([76,bitshift(ii,2)+red1,red2+grn2,grn3+blue]));
    else
      
    end
  end
  h=findobj('Tag','LEDColor');
  %set to color of picker


function btnGetLEDColors(hObject,eventdata,handles)
  hleds=[];
  for ii=1:16 %get all selected LEDs
    h=findobj('Tag',strcat('LED',num2str(ii)));
    if h.Value
      hleds=[h hleds];
    end
  end
  

function btnSelectAllLEDs(hObject,eventdata,handles)
  for ii=1:16
    h=findobj('Tag',strcat('LED',num2str(ii)));
    h.Value=1;
  end


function btnDeselectAllLEDs(hObject,eventdata,handles)
  for ii=1:16
    h=findobj('Tag',strcat('LED',num2str(ii)));
    h.Value=0;
  end

function btnZeroMotor(hObject, eventdata, handles)
 %buttons have a tag of the form 'btnMnZero_parentshortname'
% eg btnM3Zero_Setup4
  handles.api.send(char([67,hObject.Tag(5),0,0])); %set reference
%save curr pos
%subtract curr pos from target pos

  %strcat(strcat('M',hObject.Tag(5)),'CurrentSteps')
  h=findobj('Tag',strjoin({'txeM','1','CurrentSteps_Setup',hObject.Tag(end)},''));
  h.String = '0';
  h=findobj('Tag',strjoin({'txeM','1','CurrentMM_Setup',hObject.Tag(end)},''));
  h.String = '0';

%TODO: all this data conversion junk should be in Python only (use do_move)
function btnMoveMotor(hObject, eventdata, handles)
  %read targets TODO input validation
  disp('move motor')
  h=findobj('Tag',strcat(strcat(strcat('txeM',hObject.Tag(5)),'TargetSteps_Setup'),hObject.Tag(end)));
  disp(h.Tag)
  steps = int16(str2num(h.String));
  disp(steps)
%  h=findobj('Tag',strcat(strcat('M',hObject.Tag(5)),'CurrentMM'));
%  mm = int16(str2num(h.String));
disp('sign check')
  %check against limits
  sign = 0;
  if steps < 0
      sign=128; %high bit in single byte
      steps = -steps;
  end
  steps = uint16(steps);
  hi = uint8(bitor(uint16(sign),bitshift(bitand(steps,uint16(hex2dec('ff00'))),-8)));
  disp hi
  disp(hi)
  lo = uint8(steps);
  disp lo
  disp(lo)
  %format message
  %send command
  %handles.api.send(char([77,hObject.Tag(5),hi,lo])); %'M' is for move
  handles.api.send(char([77,hObject.Tag(5),hi,lo])); %'M' is for move
    %handles.api.send(char([77,'2',hi,lo])); %'M' is for move
    %  handles.api.send(char([77,'1',hi,lo])); %'M' is for move
    %    handles.api.send(char([77,'1',hi,lo])); %'M' is for move
        
  %handles.api.do_move(hObject.Tag(5),steps);

function btnMotorStepDelay(hObject, eventdata, handles)
  tagend=strsplit(hObject.Tag,'_');
  tagend=tagend(2);
  srctag=strcat('txeM',hObject.Tag(5));
  srctag=strcat(srctag,'Delay_');
  srctag=char(strcat(srctag,tagend))
  h=findobj('Tag',srctag);
  delay=uint16(str2num(h.String))
  hi=uint8(bitshift(delay,-8));
  lo=uint8(delay);
  handles.api.send(char([68,hObject.Tag(5),hi,lo]));

%returns string w/ each camera name on a newline (for popup menus)
%first string is '(No cameras detected)' or '(None)' if cameras are present
function camlist = detectCameras()
  cams=webcamlist;
  if length(cams)
    camlist='(None)\n';
  else
    camlist='(No cameras detected)';
    return;
  end
  for ii=1:length(cams)
    cam=webcam(char(cams(ii)));
    if exist('cam') && isvalid(cam)%todo: replace with try block?
      camlist=strcat(camlist,strcat(char(cams(ii)),'\n'));
    end
    clear cam; %turn off camera
  end
  camlist=strtrim(compose(camlist)); %remove trailing newline
      
%returns a string w/ each resolution on a newline (for popup menus)
%input is webcam object
function detectCameraResolutions(cam)
  reslist='(No camera selected)';
  if ~isvalid(cam)
    return;
  end
  availres=cam.AvailableResolutions;
  for ii=1:length(availres)
    reslist=strcat(reslist,strcat(char(),'\n'));
  end
  reslist=compose(reslist);
    
  

%returns array of ['Name' currentval minval maxval] for each image setting
%input is webcam object
function [camsettings] = detectCameraSettings(cam)
  camprops = properties(cam);
  camprops = camprops(4:end); %exclude Name, Resolution, and AvailableResolutions
  camsettings = [];
  val=0; min=0; max=0;
  for ii=1:length(camprops) %probe each property range
    val=cam.camprops(ii); %get current value
    try %TODO: is there a more elegant way to go about this?
      cam.camprops(ii) = 9999999999; %trigger error by trying to set absurd value
    catch E
      rangestr=strsplit(extractAfter(E.message,'between '));
      min=char(rangestr(1));
      max=char(rangestr(3));
      max=min(1:end-1); %trim the trailing '.'
      camsettings(ii) = [cam.camprops(ii) val str2num(min) str2num(max)];
    end
  end

function btnDetectCameras(hObject, eventdata, handles)
  handles.cam=webcam(2);
  guidata(hObject,handles);
  return;
  hObject.Enable='off'; % turn off 'Detect' button while detection is running
  drawnow; %immediately update GUI to reflect change
  campops=findobj('-regexp','Tag','\w*popCameraList\w*');
  camlist=detectCameras();
  if ~length(camlist)
    camlist=['(No cameras detected)'];
    enable='off';
  else
    camlist=['(None)'; camlist];
    enable = 'on';
  end

  for ii=1:length(campops) %fill all popup menus
    campops(ii).String = strtrim(compose(camlist(ii)));
    campops(ii).Enable=enable;
  end
  %find all camera toggle buttons and enable
  hObject.Enable='on'; %re-enable detection on completion
  drawnow; %update GUI
    
function btnToggleCamera(hObject, eventdata, handles)
  %check button text - proceed if not 'No Camera'
  %
    %if
  %find all camera-dependent widgets and toggle as well
  

%called on selection of a camera
function popCameraList(hObject, eventdata, handles)
  %src=find('Tag',strcat('btnCameraList_'
  campops=findobj('-regexp','Tag','\w*popCameraResolutionList\w*');
  if hObject.Value == 1 % No cam selected or available
    if strcmp(hObject.String,'(No cameras detected)')
    elseif strcmp(hObject.String,'(None)')
    end
    for ii=1:length(respops)
      respops(ii).String='(No camera selected)';
    end
  else % camera selected
  end

%  reslistlist=[];
%  for ii=1:length(camlist) %get resolutions for each cam
%    cam=webcam(char(camlist(ii)));
%    if exist('cam') && isvalid(cam)
%      reslist=[];
%        for jj=1:length(cam.AvailableResolutions)
%          reslist=[reslist cam.AvailableResolutions(jj)];
%        end
%        reslistlist=[reslistlist reslist];
%    end
%    detectCameraSettings(cam)
%    clear cam;
%  end

%called on selection of a resolution
%String of all popupmenus should have been set when camera was selected
function popCameraResolutionList(hObject, eventdata, handles)
  reslist = splitlines(hObject.String); %get resolutions into index-able form
  if exists(handles,'cam') && isvalid(handles.cam)
    handles.cam.Resolution = reslist(hObject.Value); %set camera resolution
  end %%todo: what if camera isn't active?
  respops=findobj('-regexp','Tag','\w*popCameraResolutionList\w*'); %find all resolution popups
  for ii=1:length(respops)
    respops(ii).Value = hObject.Value; %set all resolution lists to same value
  end

function onCloseFigure(src,evt,hbtn)
  hbtn.Enable='on';
  delete(src);

function btnShowVideo(hObject, eventdata, handles)
  hObject.Enable='off';
  drawnow;
  hbtn=hObject;
  f=figure('Visible','off','Position',[0 0 1200 1200],'CloseRequestFcn',@(hObject,eventdata)onCloseFigure(hObject,eventdata,hbtn));
  a=axes(f);
  f.Visible='on';
  WinOnTop(f,true); %permits always on top without block background interaction

function btnEditSymbols(hObject, eventdata, handles)
  hObject.Enable='off';
  drawnow;

function btnDetectSerialPorts(hObject, eventdata, handles)
  ports=handles.api.detect_serial_ports(); %returns list w/ elements of form (port, desc, addr)
  str='';
  for ii=1:length(ports) %build port list
    
  end
  sppops=findobj('-regexp','Tag','\w*popSerialPortList\w*'); %get all port lists (popup menus)
  for ii=1:length(sppops)
    sppops.String=str;
  end