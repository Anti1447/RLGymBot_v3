## Useful Commands

## Training Start
cd C:\Projects\RLGymBot_v3\GigaLearnBot\build	
set PYTHONHOME=C:\Users\nhafe\AppData\Local\Programs\Python\Python311
set PYTHONPATH=%PYTHONHOME%\Lib;%PYTHONHOME%\DLLs
cmake --build . --config Release
bin\GigaLearnBot.exe

## Rendering start
cd C:\Projects\RLGymBot_v3\GigaLearnBot\build
set PYTHONHOME=C:\Users\nhafe\AppData\Local\Programs\Python\Python311
set PYTHONPATH=%PYTHONHOME%\Lib;%PYTHONHOME%\DLLs
bin\GigaLearnBot.exe --render

## optional rendering
bin\GigaLearnBot.exe --render --1v1
bin\GigaLearnBot.exe --render --2v2
bin\GigaLearnBot.exe --render --3v3

## Mechanic Template
Mechanic: [name]
Trigger condition: [exact observable state that fires the reward]
Anti-farming defense: [what prevents looping the trigger cheaply]
Expected activation rate: [% of steps this should fire]
Expected weight: [relative to GoalReward = 450]
W&B metric name: [what you'll watch]
"Working" signal at 500M steps: [what does working look like in W&B?]
Existing rewards this overlaps with: [any?]