Robotic Foram Imaging System
=
This repo contains the source code for my team's 2017-2018 NCSU ECE Senior Design project: a prototype automated microfossil imaging system.

The system is intended to support paleoceanographic research by automating the extremely tedious task of isolating, imaging and identifying fossilized Foraminifera specimens.

There are three main software components, each in a different language:
- C (on the microcontroller): serial command processing, encoder monitoring, stepper motor and switch operation
- MATLAB: GUI, image processing and Python->MATLAB communication
- Python: command API, communication process, and sequencing engine for automation.

(Although we've all graduated, this project is still being actively developed, so please pardon the mess.)
