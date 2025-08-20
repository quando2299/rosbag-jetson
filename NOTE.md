[ SPS ] [ PPS ] [ SEI ] [ IDR Slice ] [ P Slice ] [ B Slice ]																																			
																																			
Each NALU has the structure:																																			
																																			
Start Code + NALU Header + Payload																																			
																																			
Now, let’s focus only on the SEI NAL Unit, since SEI is where the timestamp is carried.																																			
																																			
The SEI structure looks like this:																																			
																																			
"00 00 00 01        ; Start code (or 00 00 01)                                                                                                                                        
06                       ; NAL header (nal_unit_type=6 => SEI)                                                                                                                                        
05                       ; payloadType = 5 (user_data_unregistered)                                                                                                                                        
[User data] size  ; payloadSize                                                                                                                                        
[User data]         ; Example: ""timestamp=1755529824816""                                                                                                                                        "																																			

																															
Note:																																			
The timestamp 1755529824816 in hexadecimal 8 bytes is:																																			
0x00 0x00 0x01 0x98 0x40 0xBB 0x4A 0x30																																			
																																			
"Since the user data contains 0x00 0x00 0x01, which coincides with a Start Code, 
the RMCS decoder misinterprets the stream and fails to decode video frames."																																			
																																			
To solve this, we apply the emulation prevention technique by inserting 0x03 into the user data sequence:																																			
																																			
0x00 0x03 0x00 0x01 0x98 0x40 0xBB 0x4A 0x30																																			