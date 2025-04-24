"""
Tappie V2 BLE Client Application
Connects to Tappie device and controls system volume based on rotary encoder inputs.
"""
import asyncio
import time
import threading
from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError
from ahk import AHK

# ===== CONFIGURATION =====
# BLE UUIDs
SERVICE_UUID = "738b66f1-91b7-4f25-8ab8-31d38d56541a"
ENC_POS_UUID = "a9c8c7b4-fb55-4d27-99e4-2c14b5812546"
ENC_BUTTON_UUID = "0c2f5fbe-c20f-49ec-8c7c-ce0c9358e574"
MEDIA_SINGLEBUTTON_UUID = "9ff67916-665f-4489-b257-46d118b1e5eb"
MEDIA_DOUBLEBUTTON_UUID = "66f1ab02-c93d-44fe-8ca9-5e8bdbb2fe80"
DEVICE_NAME = "TappieTest"

# Application constants
RECONNECT_DELAY = 15  # seconds
RESET_DELAY = 10      # seconds to wait before resetting to Master
VOLUME_STEP = 5       # Volume increment/decrement per encoder step

# Audio device indices
AUDIO_DEVICES = {
    "Aux": 13,
    "Gaming": 17,
    "Media": 9,
    "Chat": 11,
    "Master": 15
}

class TappieController:
    """Main controller class for Tappie device interactions"""
    

    def __init__(self):
        """Initialize the controller"""
        self.ahk = AHK(executable_path=r"C:\Program Files\AutoHotkey\v1.1.36.02\AutoHotkeyU64.exe")
        self.selected_device = "Master"
        self.prev_enc_position = 0
        self.reset_timer = None
        self.last_volume_change = time.time()
        
    def schedule_reset(self):
        """Schedule a reset to Master after RESET_DELAY seconds"""
        if self.reset_timer:
            self.reset_timer.cancel()
        
        self.reset_timer = threading.Timer(RESET_DELAY, self._reset_to_master)
        self.reset_timer.daemon = True
        self.reset_timer.start()
    
    def _reset_to_master(self):
        """Reset selected device to Master"""
        self.selected_device = "Master"
        print("Inactivity detected - Reset to Master volume control")
    
    def roundToFive(self, x):
        """Round a number to the nearest multiple of 5"""
        return round(x / 5) * 5

    def get_device_index(self, device_name):
        """Get the device index for the currently selected device"""
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
        """Adjust volume up or down based on the parameter"""
        device_index = self.get_device_index(None)
        current_volume = self.ahk.sound_get(device_number=device_index, component_type="MASTER", control_type="VOLUME")
        current_volume = int(float(current_volume))
        current_volume = self.roundToFive(current_volume)
        if increase:
            new_volume = current_volume + VOLUME_STEP
            operation = "increased"
        else:
            new_volume = current_volume - VOLUME_STEP
            operation = "decreased"
        
        # Ensure volume stays within valid range (0-100)
        new_volume = max(0, min(100, new_volume))
        print(new_volume)
        
        self.ahk.sound_set(new_volume, device_number=device_index, component_type="MASTER", control_type="VOLUME")
        print(f"Volume {operation} to {new_volume} for device {device_index}")
        
        # Update timestamp and schedule reset
        self.last_volume_change = time.time()
        self.schedule_reset()
    
    def select_device(self, device_name):
        """Select a specific audio device by name"""
        if device_name in AUDIO_DEVICES:
            self.selected_device = device_name
            print(f"Selected device: {device_name}")
            
            # Cancel any pending reset when a device is explicitly selected
            if self.reset_timer:
                self.reset_timer.cancel()
                self.reset_timer = None
        else:
            print(f"Unknown device: {device_name}")
    
    def handle_encoder_position(self, position):
        """Handle encoder position changes"""
        position = int(position)
        
        if position == 0:
            print("Encoder position is zero, ignoring...")
            return
            
        if position > self.prev_enc_position:
            print(f"Encoder position increased: {position}")
            self.adjust_volume(increase=True)
        elif position < self.prev_enc_position:
            print(f"Encoder position decreased: {position}")
            self.adjust_volume(increase=False)
        else:
            print(f"Encoder position unchanged: {position}")
            
        self.prev_enc_position = position
    
    def handle_encoder_button(self, button_action):
        """Handle encoder button actions"""
        print(f"Received button action: {button_action}")
        
        if button_action == "single click":
            self.ahk.key_press("Media_Play_Pause")
            self.ahk.sound_play("C:\\Users\\henry\\OneDrive\\Documents\\\TappieV2\\TappieV2\\PCApp\\Pause.wav")
        elif button_action == "double click":
            self.ahk.key_press("Media_Next")
        elif button_action == "multi click":
            self.ahk.key_press("Media_Prev")
        elif button_action == "long press release":
            print("Long press detected - special action could go here")
            # Uncomment to launch Spotify
            self.ahk.run_script('Run "C:\\Users\\henry\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\Spotify.lnk"')
    
    def handle_media_button(self, button_name):
        """Handle media button actions"""
        print(f"Received media button: {button_name}")
        
        if button_name != "0":  # Ignore release notifications
            self.select_device(button_name)
            self.ahk.sound_play("C:\\Users\\henry\\OneDrive\\Documents\\\TappieV2\\TappieV2\\PCApp\\MediaChange.wav")
        
    def handle_media_double_button(self, button_name):
        """Handle double media button actions"""
        print(f"Received double media button: {button_name}")
        
        if button_name != "0":
            self.ahk.sound_set("+1", device_number=self.get_device_index(button_name), component_type="MASTER", control_type="MUTE")
    def cleanup(self):
        """Clean up resources"""
        if self.reset_timer:
            self.reset_timer.cancel()
            self.reset_timer = None


class BLEClient:
    """BLE client that connects to the Tappie device"""
    
    def __init__(self, controller):
        """Initialize with a controller instance"""
        self.controller = controller
        self.ahk = AHK(executable_path=r"C:\Program Files\AutoHotkey\v1.1.36.02\AutoHotkeyU64.exe")
        
    async def find_device(self):
        """Find the BLE device by name"""
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
        """Connect to the device with retry mechanism"""
        while True:
            device = await self.find_device()
            if not device:
                print(f"Retrying in {RECONNECT_DELAY} seconds...")
                await asyncio.sleep(RECONNECT_DELAY)
                continue
            
            try:
                client = BleakClient(device)
                await client.connect()
                print(f"Connected: {client.is_connected}")
                return client
            except BleakError as e:
                print(f"Connection error: {e}")
                print(f"Retrying in {RECONNECT_DELAY} seconds...")
                await asyncio.sleep(RECONNECT_DELAY)
    
    def setup_notification_handlers(self, client):
        """Set up notification handlers for the client"""
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
        """Run the client once connected"""
        handlers = self.setup_notification_handlers(client)
        try:
            # Get services
            services = await client.get_services()
            for service in services:
                print(f"Service: {service.uuid}")
                for char in service.characteristics:
                    print(f"  Characteristic: {char.uuid}")
                    print(f"    Properties: {char.properties}")
            
            # Start notifications
            for uuid, handler in handlers.items():
                await client.start_notify(uuid, handler)
            
            print("Listening for notifications, press Ctrl+C to stop...")

            self.ahk.sound_play("C:\\Users\\henry\\OneDrive\\Documents\\\TappieV2\\TappieV2\\PCApp\\OnConnect.wav")
            
            # Keep checking connection
            while True:
                if not client.is_connected:
                    print("Disconnected! Attempting to reconnect...")
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
        """Main loop with connection management and reconnection logic"""
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
    """Application entry point"""
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