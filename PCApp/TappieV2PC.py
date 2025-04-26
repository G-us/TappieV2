import asyncio
import time
import threading
from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError
from ahk import AHK

from win11toast import toast_async
from win11toast import notify

# ===== CONFIGURATION =====
# BLE UUIDs
SERVICE_UUID = "738b66f1-91b7-4f25-8ab8-31d38d56541a"
ENC_POS_UUID = "a9c8c7b4-fb55-4d27-99e4-2c14b5812546"
ENC_BUTTON_UUID = "0c2f5fbe-c20f-49ec-8c7c-ce0c9358e574"
MEDIA_SINGLEBUTTON_UUID = "9ff67916-665f-4489-b257-46d118b1e5eb"
MEDIA_DOUBLEBUTTON_UUID = "66f1ab02-c93d-44fe-8ca9-5e8bdbb2fe80"

DEVICE_NAME = "TappieV2"

# Application constants
RECONNECT_DELAY = 15  # seconds
RESET_DELAY = 10      # seconds to wait before resetting to Master
VOLUME_STEP = 5       # Volume increment/decrement per encoder step

# Audio device indices
AUDIO_DEVICES = {
    "Master": 15,
    "Gaming": 17,
    "Aux": 13,
    "Media": 9,
    "Chat": 11,
    
}

AUDIO_DEVICE_ICONS = {
    "Master": "C:\\Users\\henry\\OneDrive\\Documents\\\TappieV2\\TappieV2\\PCApp\\TappieIcon.ico",
    "Gaming": "C:\\Users\\henry\\OneDrive\\Documents\\\TappieV2\\TappieV2\\PCApp\\games.ico",
    "Aux": "C:\\Users\\henry\\OneDrive\\Documents\\\TappieV2\\TappieV2\\PCApp\\musical-note.ico",
    "Media": "C:\\Users\\henry\\OneDrive\\Documents\\\TappieV2\\TappieV2\\PCApp\\firefox.ico",
    "Chat": "C:\\Users\\henry\\OneDrive\\Documents\\\TappieV2\\TappieV2\\PCApp\\bubble-chat.ico",
}

class TappieController:
#Main controller class for Tappie device interactions
    

    def __init__(self):
        # Initialize the controller
        self.ahk = AHK(executable_path=r"C:\Program Files\AutoHotkey\v1.1.36.02\AutoHotkeyU64.exe")
        self.ahk.menu_tray_icon("C:\\Users\\henry\\OneDrive\\Documents\\\TappieV2\\TappieV2\\PCApp\\loading.ico")
        self.ahk.menu_tray_tooltip("Tappie V2")
        self.selected_device = "Master"
        self.prev_enc_position = 0
        self.reset_timer = None
        self.last_volume_change = time.time()
        self.previousBatteryLevel = None  # Add this line
    
    def schedule_reset(self):
        #Schedule a reset to Master after RESET_DELAY seconds
        if self.reset_timer:
            self.reset_timer.cancel()
        
        self.reset_timer = threading.Timer(RESET_DELAY, self._reset_to_master)
        self.reset_timer.daemon = True
        self.reset_timer.start()
    
    def _reset_to_master(self):
        #Reset selected device to Master#
        self.selected_device = "Master"
        self.updateToolTip(batteryLevel=None)  # Update tooltip without battery level
        print("Inactivity detected - Reset to Master volume control")
    
    def roundToFive(self, x):
        #Round a number to the nearest multiple of 5#
        return round(x / 5) * 5
    
    def updateToolTip(self, batteryLevel):
        # Update the tooltip with the current battery level
        if self.previousBatteryLevel is not None:
            print(f"previousBatteryLevel: {self.previousBatteryLevel}%")
        else:
            print("previousBatteryLevel not set")
            
        toolTipString = ""
        for audio_device in AUDIO_DEVICES:
            if self.ahk.sound_get(device_number=AUDIO_DEVICES[audio_device], component_type="MASTER", control_type="MUTE") == "On":
                if self.selected_device == audio_device:
                    toolTipString += f"→{audio_device} is muted\n"
                else:
                    toolTipString += f"{audio_device} is muted\n"
            else:

                volume = self.ahk.sound_get(device_number=AUDIO_DEVICES[audio_device], component_type='MASTER', control_type='VOLUME')
                volume_int = int(float(volume))
                if self.selected_device == audio_device:
                    toolTipString += f"→ {audio_device}: {volume_int}%\n"
                else:
                    toolTipString += f"{audio_device}: {volume_int}%\n"
        if batteryLevel is None:
            if self.previousBatteryLevel is not None:
                toolTipString += f"Battery level: {self.previousBatteryLevel}%"
            else:
                toolTipString += "Battery level: N/A"
        else:
            toolTipString += f"Battery level: {batteryLevel}%"
            self.previousBatteryLevel = batteryLevel  # Store in instance variable
            print(f"Previous battery level: {self.previousBatteryLevel}%")
            
        self.ahk.menu_tray_tooltip(toolTipString)
        self.ahk.menu_tray_icon(AUDIO_DEVICE_ICONS[self.selected_device])
        
    
    def handleBatteryLevel(self, batteryLevel):
        # Handle battery level notifications with better error handling
        try:
            # Try to convert to integer
            batteryLevel = int(batteryLevel)
            print(f"Battery level: {batteryLevel}%")
            
            # Handle low battery notification
            if batteryLevel < 20:
                print("Battery low!")
                notify("Battery low!", "aaah get freaky", audio={'silent': 'true'})
                self.ahk.menu_tray_icon("C:\\Users\\henry\\OneDrive\\Documents\\\TappieV2\\TappieV2\\PCApp\\lowBattery.ico")
                self.ahk.sound_play("C:\\Users\\henry\\OneDrive\\Documents\\\TappieV2\\TappieV2\\PCApp\\low_batterysound.mp3")
            else:
                # Reset icon if battery is okay
                self.ahk.menu_tray_icon(AUDIO_DEVICE_ICONS[self.selected_device])
        except ValueError:
            print(f"Error: Invalid battery level format: {batteryLevel}")
        except Exception as e:
            print(f"Error processing battery level: {e}")

    def get_device_index(self, device_name):
        #Get the device index for the currently selected device#
        if device_name == None:
            device_id = AUDIO_DEVICES.get(self.selected_device)
        else:
            device_id = AUDIO_DEVICES.get(device_name)
        if device_id is None:
            print(f"Invalid device selection: {self.selected_device}")
            return AUDIO_DEVICES["Master"]
        print(f"Selected device: {self.selected_device}")
        return device_id
    
    def adjust_volume(self, increase=True):
        #Adjust volume up or down based on the parameter#
        if self.reset_timer:
            self.reset_timer.cancel()
            self.reset_timer = None
        device_index = self.get_device_index(None)
        if self.ahk.sound_get(device_number=device_index, component_type="MASTER", control_type="MUTE") == "On":
            print("Device is muted, cannot adjust volume")
            return
        else:
            current_volume = self.ahk.sound_get(device_number=device_index, component_type="MASTER", control_type="VOLUME")
            current_volume = int(float(current_volume))
            current_volume = self.roundToFive(current_volume)
            if increase:
                new_volume = current_volume + VOLUME_STEP
                operation = "increased"
                if new_volume >= 100:
                    self.ahk.sound_play("C:\\Users\\henry\\OneDrive\\Documents\\\TappieV2\\TappieV2\\PCApp\\maxVolumeSound.wav")
            else:
                new_volume = current_volume - VOLUME_STEP
                operation = "decreased"
                if new_volume <= 0:
                    self.ahk.sound_play("C:\\Users\\henry\\OneDrive\\Documents\\\TappieV2\\TappieV2\\PCApp\\minVolumeSound.wav")
            
            # Ensure volume stays within valid range (0-100)
            new_volume = max(0, min(100, new_volume))
            print(new_volume)
            
            self.ahk.sound_set(new_volume, device_number=device_index, component_type="MASTER", control_type="VOLUME")
            print(f"Volume {operation} to {new_volume} for device {device_index}")
            
            # Update timestamp and schedule reset
            self.last_volume_change = time.time()
            self.schedule_reset()
    
    def select_device(self, device_name):
        #Select a specific audio device by name#
        if device_name in AUDIO_DEVICES:
            self.selected_device = device_name
            self.updateToolTip(batteryLevel=None)  # Update tooltip without battery level
            print(f"Selected device: {device_name}")
            self.ahk.menu_tray_icon(AUDIO_DEVICE_ICONS[device_name])
            # Cancel any pending reset when a device is explicitly selected
            if self.reset_timer:
                self.reset_timer.cancel()
                self.reset_timer = None
                self.last_volume_change = time.time()
                self.schedule_reset()
        else:
            print(f"Unknown device: {device_name}")
    
    def handle_encoder_position(self, encData):
        # Handle encoder position changes with better error handling
        try:
            # Split the combined string (format: "position batteryLevel")
            parts = encData.split(" ", 1)  # Split only on first space
            
            if len(parts) >= 2:
                position = parts[0]
                batteryLevel = parts[1]
                # Process battery level
                try:
                    self.handleBatteryLevel(batteryLevel)
                    self.updateToolTip(batteryLevel)
                except Exception as e:
                    print(f"Error handling battery level: {e}")
            else:
                # If there's no battery level data, just use the position
                position = parts[0]
                print("Warning: No battery level data received")
            
            # Process position
            if position == "reset":
                print("Encoder position reset")
                self.prev_enc_position = 0
                return
            
            # Convert position to integer with error handling
            try:
                current_position = int(position)
                
                if current_position > self.prev_enc_position:
                    print(f"Encoder position increased: {position}")
                    self.adjust_volume(increase=True)
                elif current_position < self.prev_enc_position:
                    print(f"Encoder position decreased: {position}")
                    self.adjust_volume(increase=False)
                else:
                    print(f"Encoder position unchanged: {position}")
                    
                self.prev_enc_position = current_position
                
            except ValueError:
                print(f"Error: Could not convert position '{position}' to integer")
                
        except Exception as e:
            print(f"Error processing encoder data '{encData}': {e}")
    
    def handle_encoder_button(self, button_action):
        #Handle encoder button actions#
        print(f"Received button action: {button_action}")
        
        if button_action == "single click":
            self.ahk.key_press("Media_Play_Pause")
            self.ahk.sound_play("C:\\Users\\henry\\OneDrive\\Documents\\\TappieV2\\TappieV2\\PCApp\\Pause.wav")
        elif button_action == "double click":
            self.ahk.key_press("Media_Next")
        elif button_action == "multi click":
            self.ahk.key_press("Media_Prev")
        elif button_action == "long press release":
            self.ahk.run_script('Run "C:\\Users\\henry\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\Spotify.lnk"')
    
    def handle_media_button(self, button_name):
        #Handle media button actions#
        print(f"Received media button: {button_name}")
        
        if button_name != "0":  # Ignore release notifications
            self.select_device(button_name)
            notify(f"Selected device: {button_name}", "aaah get freaky", audio={'silent': 'true'})
            self.ahk.sound_play("C:\\Users\\henry\\OneDrive\\Documents\\\TappieV2\\TappieV2\\PCApp\\MediaChange.wav")
        
    def handle_media_double_button(self, button_name):
        #Handle double media button actions#
        print(f"Received double media button: {button_name}")
        
        if button_name != "0":
            self.ahk.sound_set("+1", device_number=self.get_device_index(button_name), component_type="MASTER", control_type="MUTE")
            self.updateToolTip(batteryLevel=None)  # Update tooltip without battery level


    def cleanup(self):
        #Clean up resources#
        if self.reset_timer:
            self.reset_timer.cancel()
            self.reset_timer = None


class BLEClient:
    #BLE client that connects to the Tappie device#
    
    def __init__(self, controller):
        #Initialize with a controller instance#
        self.controller = controller
        
    async def find_device(self):
        #Find the BLE device by name#
        print(f"Scanning for {DEVICE_NAME}...")
        device = await BleakScanner.find_device_by_name(DEVICE_NAME)
        
        if not device:
            print(f"Could not find {DEVICE_NAME}")
            print("Available devices:")
            devices = await BleakScanner.discover()
            for d in devices:
                print(f"  {d.name}: {d.address}")
            return None
        
        print(f"Found {device.name} ({device.address})")
        return device
    
    async def connect_with_retry(self):
        #Connect to the device with retry mechanism#
        while True:
            device = await self.find_device()
            if not device:
                print(f"Retrying in {RECONNECT_DELAY} seconds...")
                await asyncio.sleep(RECONNECT_DELAY)
                continue
            
            try:
                client = BleakClient(device, winrt=dict(use_cached_services=False))
                await client.connect()
                print(f"Connected: {client.is_connected}")
                self.controller.ahk.menu_tray_tooltip("Connected to Tappie V2")
                #notify("Connection Established with Tappie V2", "aaah get freaky", audio={'silent': 'true'}, duration=0.5)
                return client
            except BleakError as e:
                print(f"Connection error: {e}")
                print(f"Retrying in {RECONNECT_DELAY} seconds...")
                await asyncio.sleep(RECONNECT_DELAY)
    
    def setup_notification_handlers(self, client):
        #Set up notification handlers for the client#
        async def enc_pos_handler(_, data):
            self.controller.handle_encoder_position(data.decode())
            
        async def enc_button_handler(_, data):
            self.controller.handle_encoder_button(data.decode())
            
        async def media_button_handler(_, data):
            self.controller.handle_media_button(data.decode())

        async def media_double_button_handler(_, data):
            self.controller.handle_media_double_button(data.decode())
            
            
        return {
            ENC_POS_UUID: enc_pos_handler,
            ENC_BUTTON_UUID: enc_button_handler,
            MEDIA_SINGLEBUTTON_UUID: media_button_handler,
            MEDIA_DOUBLEBUTTON_UUID: media_double_button_handler
        }
    
    async def run_client(self, client):
        #Run the client once connected#
        handlers = self.setup_notification_handlers(client)
        try:
            # Get services with detailed property information
            services = await client.get_services()
            for service in services:
                print(f"Service: {service.uuid}")
                for char in service.characteristics:
                    print(f"  Characteristic: {char.uuid}")
                    print(f"    Properties: {char.properties}")
                    print(f"    Has Notify: {'Notify' in char.properties}")
        
            # Start notifications with better error handling and delays
            for uuid, handler in handlers.items():
                try:
                    print(f"Starting notification for {uuid}...")
                    await client.start_notify(uuid, handler)
                    print(f"Successfully started notification for {uuid}")
                    # Add a small delay between starting each notification to prevent BLE stack issues
                    await asyncio.sleep(0.5)
                except Exception as e:
                    print(f"Error starting notification for {uuid}: {e}")
                    continue
        
            print("Listening for notifications, press Ctrl+C to stop...")
            
            #notify("Ready to talk to Tappie V2", "aaah get freaky", audio={'silent': 'true'})
            self.controller.ahk.menu_tray_tooltip("Ready to talk to Tappie V2")
            self.controller.ahk.menu_tray_icon("C:\\Users\\henry\\OneDrive\\Documents\\\TappieV2\\TappieV2\\PCApp\\TappieIcon.ico")
            self.controller.updateToolTip(batteryLevel=None)  # Update tooltip without battery level
            
            # Keep checking connection
            while True:
                if not client.is_connected:
                    print("Disconnected! Attempting to reconnect...")
                    notify("Disconnected from Tappie V2", "aaah get freaky")
                    self.controller.ahk.menu_tray_tooltip("Disconnected from Tappie V2")
                    self.controller.ahk.menu_tray_icon("C:\\Users\\henry\\OneDrive\\Documents\\\TappieV2\\TappieV2\\PCApp\\loading.ico")
                    break
                await asyncio.sleep(0.5)
                
        except Exception as e:
            print(f"Error during client operation: {e}")
        finally:
            # Clean up
            if client.is_connected:
                try:
                    for uuid in handlers.keys():
                        await client.stop_notify(uuid)
                    print("Notifications stopped")
                    await client.disconnect()
                except Exception as e:
                    print(f"Error during disconnect: {e}")
    
    async def main_loop(self):
        #Main loop with connection management and reconnection logic#
        try:
            while True:
                # Connect with retry
                client = await self.connect_with_retry()
                
                # Run until disconnection
                await self.run_client(client)
                
                # If we get here, connection was lost
                print(f"Reconnecting in {RECONNECT_DELAY} seconds...")
                await asyncio.sleep(RECONNECT_DELAY)
                
        except asyncio.CancelledError:
            print("Task was cancelled")
        except KeyboardInterrupt:
            print("Script terminated by user")


async def main():
    #Application entry point#
    controller = TappieController()
    client = BLEClient(controller)
    
    try:
        await client.main_loop()
    finally:
        controller.cleanup()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("Script terminated by user")