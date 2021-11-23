#!/bin/bash 

# memory issues; this needs to be tried also with gdb / valgrind
scuff-microstrip --geometry /home/mmn/.local/share/scuff-em/examples/MicrostripDevices/scuffgeoFiles/EFAntenna_L8_Coarse.scuffgeo --PortFile /home/mmn/.local/share/scuff-em/examples/MicrostripDevices/portFiles/EFAntenna.ports --minFreq 0.5 --maxFreq 20 --numFreqs 40 --ZParameters --Eps 2.2 --h 0.794

