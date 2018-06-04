%convenience function - used by process to communicate to UI
%src - 0 (microcontroller, (4 byte messages))
%      1 (sequencing engine (engine-specific messages))
%      2 (comms process (control messages, etc.))
%msg - variable type containing message information
%message types:
% log
%   message - string
% mcu
%   message - string
%     error
%       source - string
%       code - number (or string?)
%     arrive
%       encoder - string
%       position - signed mag 16 bit
% com
%   message - string
%     shutdown
%     serial_up
%     serial_down
%     api_up
%     api_down
% seq
%   message - string
%     get:
%       {"symbolname"}
%       {"all"}
function result=RFIS_notify(src,msg)
  result=int64(0);
  rfis=findobj('Tag','RFIS'); %get handle to active RFIS GUI fig
  if length(rfis) == 0
    %disp('RFIS_notify: UI figure not found')
    return;
  elseif length(rfis) > 1
    %disp('RFIS_notify: multiple figures with same tag')
    return;
  end
  handles=guidata(rfis);
  switch src
    case py.rfis.SRC_MCU
%      disp('RFIS_notify: source=microcontroller')
%      disp(msg)
      %handle E, A, ?
      %expecting a vector, 4 elements
    case py.rfis.SRC_SEQ
%      disp('RFIS_notify: source=sequencer')
%      disp(msg)
      
      %
    case py.rfis.SRC_COM
      disp('RFIS_notify: source=process')
      disp(msg)
      try
        data=jsondecode(msg);
      catch E
        disp('RFIS_notify: process message format error')
        return;
      end
      if strcmp(data.type,'log')
        h=findobj('Tag','txeLog');
%        try
%          f=fileread('rfis_proc.log')
          %str=strcat(strcat(data.message,'\n'),h.String);
%          data.message
          h.String={data.message,char(h.String)};%sprintf('%s\n%s',data.message,h.String);
%        catch E
%          disp('RFIS_notify: failed to load process log')
%          return;
      end
    otherwise
      disp('RFIS_notify: invalid source')
      disp(msg)
  end
  %messages from process: (vector type (list in python))
  %Error (set code)
  %error clear (on clear, reset)
  %Arrive
  %Trigger?
  %calibrated (all motors zero'd, not reset)
  %uncalibrated (got reset)
  
  %program/other - cell type (STOP WHEN MATLAB GOES AWAY. just hang where you are)
  % active (name?)
  % done (timeout, terminated normally, ?)
  % set symbol
  % get symbol
  % toggle detector
  % foram detection result (in symbol?)
  % Failed to load, parse, validate, or execute program
  % invalid command (remote or local)