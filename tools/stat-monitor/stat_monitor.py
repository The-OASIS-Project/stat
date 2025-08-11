#!/usr/bin/env python3
"""
OASIS STAT Monitor - Real-time telemetry display
Connects to MQTT broker and displays STAT telemetry data in a user-friendly GUI
"""

import tkinter as tk
from tkinter import ttk, messagebox
import json
import paho.mqtt.client as mqtt
import threading
import time
from datetime import datetime
import argparse


class StatMonitor:
   def __init__(self, mqtt_host="localhost", mqtt_port=1883, mqtt_topic="stat"):
      self.mqtt_host = mqtt_host
      self.mqtt_port = mqtt_port
      self.mqtt_topic = mqtt_topic
      self.debug_mode = False  # Can be enabled externally
      
      # Data storage - separate different battery sources
      self.data = {
         'battery_sources': {},    # Dynamic battery sources
         'system': {},
         'connection': {'status': 'Disconnected', 'last_update': None}
      }
      
      # Track active tabs
      self.battery_tabs = {}  # source_name -> tab_frame
      self.battery_widgets = {}  # source_name -> widgets dict
      
      # Create main window
      self.root = tk.Tk()
      self.root.title(f"OASIS STAT Monitor - {mqtt_host}:{mqtt_port}")
      self.root.geometry("1300x1400")  # Larger window to fit all data
      self.root.configure(bg='#2b2b2b')
      
      # Configure styles
      self.setup_styles()
      
      # Create UI
      self.create_widgets()
      
      # MQTT client
      self.mqtt_client = None
      self.setup_mqtt()
      
      # Start update thread
      self.running = True
      self.update_thread = threading.Thread(target=self.update_loop, daemon=True)
      self.update_thread.start()
      
      # Handle window close
      self.root.protocol("WM_DELETE_WINDOW", self.on_closing)
   
   def setup_styles(self):
      """Configure ttk styles for dark theme"""
      style = ttk.Style()
      
      # Configure dark theme
      style.theme_use('clam')
      
      # Main colors
      bg_color = '#2b2b2b'
      fg_color = '#ffffff'
      select_bg = '#404040'
      
      style.configure('Dark.TLabel', background=bg_color, foreground=fg_color)
      style.configure('Title.TLabel', background=bg_color, foreground='#00ff00', 
                     font=('Arial', 14, 'bold'))
      style.configure('Status.TLabel', background=bg_color, foreground='#ffff00',
                     font=('Arial', 10, 'bold'))
      style.configure('Critical.TLabel', background=bg_color, foreground='#ff4444',
                     font=('Arial', 10, 'bold'))
      style.configure('Warning.TLabel', background=bg_color, foreground='#ffaa00',
                     font=('Arial', 10, 'bold'))
      style.configure('Normal.TLabel', background=bg_color, foreground='#44ff44',
                     font=('Arial', 10, 'bold'))
      
      style.configure('Dark.TFrame', background=bg_color)
      style.configure('Dark.TNotebook', background=bg_color)
      style.configure('Dark.TNotebook.Tab', background=select_bg, foreground=fg_color)
      
      # Charge state colors
      style.configure('Charging.TLabel', background=bg_color, foreground='#44ff44',
                     font=('Arial', 10, 'bold'))
      style.configure('Discharging.TLabel', background=bg_color, foreground='#ffaa00',
                     font=('Arial', 10, 'bold'))
      style.configure('Idle.TLabel', background=bg_color, foreground='#aaaaaa',
                     font=('Arial', 10, 'bold'))
      
      # Treeview (for BMS cell data)
      style.configure('Dark.Treeview', background='#333333', foreground=fg_color,
                     fieldbackground='#333333')
      style.configure('Dark.Treeview.Heading', background=select_bg, foreground=fg_color)
   
   def create_widgets(self):
      """Create the main UI widgets"""
      # Main container
      main_frame = ttk.Frame(self.root, style='Dark.TFrame')
      main_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
      
      # Header
      header_frame = ttk.Frame(main_frame, style='Dark.TFrame')
      header_frame.pack(fill=tk.X, pady=(0, 10))
      
      ttk.Label(header_frame, text="OASIS STAT Monitor", 
               style='Title.TLabel').pack(side=tk.LEFT)
      
      self.connection_label = ttk.Label(header_frame, text="Disconnected", 
                                       style='Critical.TLabel')
      self.connection_label.pack(side=tk.RIGHT)
      
      # Create notebook for tabs
      self.notebook = ttk.Notebook(main_frame, style='Dark.TNotebook')
      self.notebook.pack(fill=tk.BOTH, expand=True)
      
      # System tab (always present)
      self.create_system_tab()
      
      # Battery tabs will be created dynamically as sources are detected
   
   def create_battery_tab(self, source_name, payload_data):
      """Create battery monitoring tab for a specific source"""
      if source_name in self.battery_tabs:
         return  # Tab already exists
      
      # Create the tab frame
      battery_frame = ttk.Frame(self.notebook, style='Dark.TFrame')
      self.notebook.add(battery_frame, text=source_name)
      
      # Store the tab frame
      self.battery_tabs[source_name] = battery_frame
      
      # Create widgets dictionary for this source
      widgets = {
         'labels': {},
         'progress': None,
         'percent_label': None
      }
      
      # Battery overview
      overview_frame = ttk.LabelFrame(battery_frame, text="Battery Overview", style='Dark.TFrame')
      overview_frame.pack(fill=tk.X, padx=5, pady=5)
      
      # Data source indicator
      source_frame = ttk.Frame(overview_frame, style='Dark.TFrame')
      source_frame.pack(fill=tk.X, pady=(0, 5))
      ttk.Label(source_frame, text="Data Source:", style='Dark.TLabel').pack(side=tk.LEFT)
      source_label = ttk.Label(source_frame, text=source_name, style='Status.TLabel')
      source_label.pack(side=tk.LEFT, padx=10)
      widgets['source_label'] = source_label
      
      # Create grid for battery info
      labels = self.get_labels_for_source(source_name, payload_data)
      
      # Create rows of data in pairs
      for i in range(0, len(labels), 2):
         row_frame = ttk.Frame(overview_frame, style='Dark.TFrame')
         row_frame.pack(fill=tk.X, pady=2)
         
         # Left column
         left_frame = ttk.Frame(row_frame, style='Dark.TFrame')
         left_frame.pack(side=tk.LEFT, fill=tk.X, expand=True)
         
         label_text, key, unit = labels[i]
         ttk.Label(left_frame, text=label_text, style='Dark.TLabel').pack(side=tk.LEFT)
         value_label = ttk.Label(left_frame, text="--", style='Dark.TLabel')
         value_label.pack(side=tk.LEFT, padx=(5, 2))
         ttk.Label(left_frame, text=unit, style='Dark.TLabel').pack(side=tk.LEFT)
         widgets['labels'][key] = value_label
         
         # Right column (if exists)
         if i + 1 < len(labels):
            right_frame = ttk.Frame(row_frame, style='Dark.TFrame')
            right_frame.pack(side=tk.RIGHT, fill=tk.X, expand=True)
            
            label_text, key, unit = labels[i + 1]
            ttk.Label(right_frame, text=label_text, style='Dark.TLabel').pack(side=tk.LEFT)
            value_label = ttk.Label(right_frame, text="--", style='Dark.TLabel')
            value_label.pack(side=tk.LEFT, padx=(5, 2))
            ttk.Label(right_frame, text=unit, style='Dark.TLabel').pack(side=tk.LEFT)
            widgets['labels'][key] = value_label
      
      # Battery status bar (only for battery sources)
      if 'battery_level' in [label[1] for label in labels]:
         status_frame = ttk.Frame(battery_frame, style='Dark.TFrame')
         status_frame.pack(fill=tk.X, padx=5, pady=5)
         
         ttk.Label(status_frame, text="Battery Level:", style='Dark.TLabel').pack(side=tk.LEFT)
         
         progress = ttk.Progressbar(status_frame, length=300, mode='determinate')
         progress.pack(side=tk.LEFT, padx=10)
         widgets['progress'] = progress
         
         percent_label = ttk.Label(status_frame, text="0%", style='Dark.TLabel')
         percent_label.pack(side=tk.LEFT, padx=5)
         widgets['percent_label'] = percent_label
      
      # Store widgets for this source
      self.battery_widgets[source_name] = widgets
      
      # Create cells section if cell data is available
      if payload_data.get('cells'):
         self.create_cells_section(battery_frame, widgets)
      
      # Create temperature sensors section if temp data is available
      if payload_data.get('temperatures'):
         self.create_temperatures_section(battery_frame, widgets)
      
      # Create faults section if fault data is available
      if (payload_data.get('critical_faults') or payload_data.get('warning_faults') or 
          payload_data.get('faults')):
         self.create_faults_section(battery_frame, widgets)
      
      if self.debug_mode:
         print(f"[DEBUG] Created tab for {source_name}")
   
   def get_labels_for_source(self, source_name, payload_data):
      """Get appropriate labels based on source type"""
      # Base labels for all battery sources
      base_labels = [
         ('Voltage:', 'voltage', 'V'),
         ('Current:', 'current', 'A'),
         ('Power:', 'power', 'W'),
         ('Battery Level:', 'battery_level', '%'),
      ]
      
      # Additional labels based on source type and available data
      additional_labels = []
      
      if 'battery_status' in payload_data:
         additional_labels.append(('Status:', 'battery_status', ''))
      
      if 'time_remaining_fmt' in payload_data:
         additional_labels.append(('Time Remaining:', 'time_remaining_fmt', ''))
      
      if 'temperature' in payload_data:
         additional_labels.append(('Temperature:', 'temperature', '°C'))
      
      if 'battery_chemistry' in payload_data:
         additional_labels.append(('Chemistry:', 'battery_chemistry', ''))
      
      # BMS-specific labels
      if 'Daly BMS' in source_name:
         if 'charge_fet' in payload_data:
            additional_labels.extend([
               ('Charge FET:', 'charge_fet', ''),
               ('Discharge FET:', 'discharge_fet', ''),
            ])
         if 'charging_state' in payload_data:
            additional_labels.append(('State:', 'charging_state', ''))
         if 'cycles' in payload_data:
            additional_labels.append(('Cycles:', 'cycles', ''))
         if 'remaining_capacity_mah' in payload_data:
            additional_labels.append(('Remaining:', 'remaining_capacity_mah', 'mAh'))
         if 'vmax' in payload_data:
            additional_labels.extend([
               ('Cell Vmax:', 'vmax', 'V'),
               ('Cell Vmin:', 'vmin', 'V'),
            ])
         if 'vdelta' in payload_data:
            additional_labels.append(('Voltage Delta:', 'vdelta', 'V'))
         if 'tmax' in payload_data:
            additional_labels.extend([
               ('Temp Max:', 'tmax', '°C'),
               ('Temp Min:', 'tmin', '°C'),
            ])
         if 'charger_present' in payload_data:
            additional_labels.extend([
               ('Charger Present:', 'charger_present', ''),
               ('Load Present:', 'load_present', ''),
            ])
      
      # Unified Battery specific labels
      if 'Unified Battery' in source_name:
         # Always add sources label for unified battery
         additional_labels.append(('Data Sources:', 'sources_str', ''))
         if 'status_reason' in payload_data:
            additional_labels.append(('Status Reason:', 'status_reason', ''))
         if 'critical_fault_count' in payload_data:
            additional_labels.extend([
               ('Critical Faults:', 'critical_fault_count', ''),
               ('Warning Faults:', 'warning_fault_count', ''),
            ])
         if 'battery_cells_series' in payload_data:
            additional_labels.extend([
               ('Cells (Series):', 'battery_cells_series', ''),
               ('Cells (Parallel):', 'battery_cells_parallel', ''),
            ])
         if 'battery_nominal_voltage' in payload_data:
            additional_labels.append(('Nominal Voltage:', 'battery_nominal_voltage', 'V'))
         if 'charging_state' in payload_data:
            additional_labels.append(('State:', 'charging_state', ''))
      
      return base_labels + additional_labels
   
   def create_cells_section(self, parent_frame, widgets):
      """Create cell voltages section"""
      cells_frame = ttk.LabelFrame(parent_frame, text="Cell Voltages", style='Dark.TFrame')
      cells_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
      
      # Create treeview for cell data
      columns = ('voltage', 'balance', 'status')
      cells_tree = ttk.Treeview(cells_frame, columns=columns, height=8, style='Dark.Treeview')
      
      # Configure columns with proper widths
      cells_tree.heading('#0', text='Cell')
      cells_tree.heading('voltage', text='Voltage (V)')
      cells_tree.heading('balance', text='Balancing')
      cells_tree.heading('status', text='Status')
      
      cells_tree.column('#0', width=80, minwidth=80)
      cells_tree.column('voltage', width=120, minwidth=120)
      cells_tree.column('balance', width=100, minwidth=100)
      cells_tree.column('status', width=100, minwidth=100)
      
      # Configure row height to prevent overlapping
      style = ttk.Style()
      style.configure('Dark.Treeview', rowheight=25)
      
      # Add scrollbar
      scrollbar = ttk.Scrollbar(cells_frame, orient=tk.VERTICAL, command=cells_tree.yview)
      cells_tree.configure(yscrollcommand=scrollbar.set)
      
      # Pack treeview and scrollbar
      cells_tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
      scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
      
      widgets['cells_tree'] = cells_tree
   
   def create_temperatures_section(self, parent_frame, widgets):
      """Create temperature sensors section"""
      temps_frame = ttk.LabelFrame(parent_frame, text="Temperature Sensors", style='Dark.TFrame')
      temps_frame.pack(fill=tk.X, padx=5, pady=5)
      
      # Create treeview for temperature data
      columns = ('temperature',)
      temps_tree = ttk.Treeview(temps_frame, columns=columns, height=4, style='Dark.Treeview')
      
      # Configure columns
      temps_tree.heading('#0', text='Sensor')
      temps_tree.heading('temperature', text='Temperature (°C)')
      
      temps_tree.column('#0', width=120, minwidth=120)
      temps_tree.column('temperature', width=150, minwidth=150)
      
      # Configure row height
      style = ttk.Style()
      style.configure('Dark.Treeview', rowheight=25)
      
      temps_tree.pack(fill=tk.X, padx=5, pady=5)
      
      widgets['temps_tree'] = temps_tree
   
   def create_faults_section(self, parent_frame, widgets):
      """Create faults section"""
      faults_frame = ttk.LabelFrame(parent_frame, text="Active Faults", style='Dark.TFrame')
      faults_frame.pack(fill=tk.X, padx=5, pady=5)
      
      # Create text widget for faults
      faults_text = tk.Text(faults_frame, height=6, bg='#333333', fg='#ffffff',
                           font=('Courier', 9), wrap=tk.WORD)
      faults_text.pack(fill=tk.X, padx=5, pady=5)
      
      widgets['faults_text'] = faults_text
   
   def create_system_tab(self):
      """Create system monitoring tab"""
      system_frame = ttk.Frame(self.notebook, style='Dark.TFrame')
      self.notebook.add(system_frame, text="System")
      
      # System metrics
      metrics_frame = ttk.LabelFrame(system_frame, text="System Metrics", style='Dark.TFrame')
      metrics_frame.pack(fill=tk.X, padx=5, pady=5)
      
      self.system_labels = {}
      
      # CPU
      cpu_frame = ttk.Frame(metrics_frame, style='Dark.TFrame')
      cpu_frame.pack(fill=tk.X, pady=5)
      
      ttk.Label(cpu_frame, text="CPU Usage:", style='Dark.TLabel').pack(side=tk.LEFT)
      self.cpu_progress = ttk.Progressbar(cpu_frame, length=200, mode='determinate')
      self.cpu_progress.pack(side=tk.LEFT, padx=10)
      self.system_labels['cpu'] = ttk.Label(cpu_frame, text="0%", style='Dark.TLabel')
      self.system_labels['cpu'].pack(side=tk.LEFT, padx=5)
      
      # Memory
      memory_frame = ttk.Frame(metrics_frame, style='Dark.TFrame')
      memory_frame.pack(fill=tk.X, pady=5)
      
      ttk.Label(memory_frame, text="Memory Usage:", style='Dark.TLabel').pack(side=tk.LEFT)
      self.memory_progress = ttk.Progressbar(memory_frame, length=200, mode='determinate')
      self.memory_progress.pack(side=tk.LEFT, padx=10)
      self.system_labels['memory'] = ttk.Label(memory_frame, text="0%", style='Dark.TLabel')
      self.system_labels['memory'].pack(side=tk.LEFT, padx=5)
      
      # Fan
      fan_frame = ttk.Frame(metrics_frame, style='Dark.TFrame')
      fan_frame.pack(fill=tk.X, pady=5)
      
      ttk.Label(fan_frame, text="Fan Speed:", style='Dark.TLabel').pack(side=tk.LEFT)
      self.system_labels['fan_rpm'] = ttk.Label(fan_frame, text="-- RPM", style='Dark.TLabel')
      self.system_labels['fan_rpm'].pack(side=tk.LEFT, padx=10)
      
      self.system_labels['fan_load'] = ttk.Label(fan_frame, text="(-- %)", style='Dark.TLabel')
      self.system_labels['fan_load'].pack(side=tk.LEFT, padx=5)
      
      # System Power Channels (INA3221)
      power_frame = ttk.LabelFrame(system_frame, text="System Power Channels", style='Dark.TFrame')
      power_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
      
      # Create treeview for power channel data
      columns = ('voltage', 'current', 'power', 'shunt')
      self.power_tree = ttk.Treeview(power_frame, columns=columns, height=6, style='Dark.Treeview')
      
      # Configure columns
      self.power_tree.heading('#0', text='Channel')
      self.power_tree.heading('voltage', text='Voltage (V)')
      self.power_tree.heading('current', text='Current (A)')
      self.power_tree.heading('power', text='Power (W)')
      self.power_tree.heading('shunt', text='Shunt (Ω)')
      
      self.power_tree.column('#0', width=150, minwidth=150)
      self.power_tree.column('voltage', width=120, minwidth=120)
      self.power_tree.column('current', width=120, minwidth=120)
      self.power_tree.column('power', width=120, minwidth=120)
      self.power_tree.column('shunt', width=120, minwidth=120)
      
      # Configure row height
      style = ttk.Style()
      style.configure('Dark.Treeview', rowheight=25)
      
      self.power_tree.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
   
   def setup_mqtt(self):
      """Setup MQTT client"""
      try:
         self.mqtt_client = mqtt.Client()
         self.mqtt_client.on_connect = self.on_mqtt_connect
         self.mqtt_client.on_message = self.on_mqtt_message
         self.mqtt_client.on_disconnect = self.on_mqtt_disconnect
         
         self.mqtt_client.connect(self.mqtt_host, self.mqtt_port, 60)
         self.mqtt_client.loop_start()
         
      except Exception as e:
         print(f"MQTT connection failed: {e}")
         self.data['connection']['status'] = f'Error: {e}'
   
   def on_mqtt_connect(self, client, userdata, flags, rc):
      """MQTT connection callback"""
      if rc == 0:
         print(f"Connected to MQTT broker {self.mqtt_host}:{self.mqtt_port}")
         self.data['connection']['status'] = 'Connected'
         client.subscribe(self.mqtt_topic)
      else:
         print(f"MQTT connection failed with code {rc}")
         self.data['connection']['status'] = f'Connection failed ({rc})'
   
   def on_mqtt_disconnect(self, client, userdata, rc):
      """MQTT disconnection callback"""
      print("Disconnected from MQTT broker")
      self.data['connection']['status'] = 'Disconnected'
   
   def on_mqtt_message(self, client, userdata, msg):
      """Handle incoming MQTT messages"""
      try:
         payload = json.loads(msg.payload.decode())
         device_type = payload.get('device', '')
         device_subtype = payload.get('type', '')
         
         if self.debug_mode:
            print(f"[DEBUG] Received: device='{device_type}', type='{device_subtype}', size={len(msg.payload)} bytes")
         
         self.data['connection']['last_update'] = datetime.now()
         
         # Determine source name for battery data
         source_name = None
         if device_type == 'Battery':
            if device_subtype == 'INA238':
               source_name = 'INA238 Power Monitor'
            elif device_subtype == 'DalyBMS':
               source_name = 'Daly BMS'
            else:
               source_name = 'Battery Monitor'
         elif device_type == 'BatteryStatus':
            source_name = 'Unified Battery'
         elif device_type == 'CPU':
            self.data['system']['cpu_usage'] = payload.get('usage', 0)
         elif device_type == 'Memory':
            self.data['system']['memory_usage'] = payload.get('usage', 0)
         elif device_type == 'Fan':
            self.data['system']['fan_rpm'] = payload.get('rpm', 0)
            self.data['system']['fan_load'] = payload.get('load', 0)
         elif device_type == 'SystemPower':
            # INA3221 multi-channel data
            self.data['system']['power_channels'] = payload.get('channels', [])
            if self.debug_mode:
               print(f"[DEBUG] SystemPower data: {len(payload.get('channels', []))} channels")
         
         # Handle battery data sources
         if source_name:
            # Store the data
            if source_name not in self.data['battery_sources']:
               self.data['battery_sources'][source_name] = {}
            
            self.data['battery_sources'][source_name].update(payload)
            
            # Create tab if it doesn't exist (call from main thread)
            if source_name not in self.battery_tabs:
               # Schedule tab creation on main thread
               self.root.after(0, lambda sn=source_name, pd=payload.copy(): self.create_battery_tab(sn, pd))
            
            if self.debug_mode:
               print(f"[DEBUG] Updated {source_name} data")
            
      except json.JSONDecodeError as e:
         print(f"JSON decode error: {e}")
      except Exception as e:
         print(f"Message processing error: {e}")
   
   def update_loop(self):
      """Main update loop"""
      while self.running:
         try:
            self.root.after(0, self.update_ui)
            time.sleep(1)
         except Exception as e:
            print(f"Update loop error: {e}")
            break
   
   def update_ui(self):
      """Update UI with current data"""
      try:
         # Update connection status
         status = self.data['connection']['status']
         last_update = self.data['connection']['last_update']
         
         if status == 'Connected' and last_update:
            age = (datetime.now() - last_update).total_seconds()
            if age > 30:  # No data for 30 seconds
               status_text = f"Connected (No data for {int(age)}s)"
               style = 'Warning.TLabel'
            else:
               status_text = f"Connected (Last: {last_update.strftime('%H:%M:%S')})"
               style = 'Normal.TLabel'
         else:
            status_text = status
            style = 'Critical.TLabel'
         
         self.connection_label.config(text=status_text, style=style)
         
         # Update all battery tabs
         for source_name in self.battery_tabs.keys():
            self.update_battery_tab(source_name)
         
         # Update system data
         self.update_system_display()
         
      except Exception as e:
         print(f"UI update error: {e}")
   
   def update_battery_tab(self, source_name):
      """Update a specific battery tab"""
      if source_name not in self.battery_widgets:
         return
      
      source_data = self.data['battery_sources'].get(source_name, {})
      widgets = self.battery_widgets[source_name]
      
      # Update basic battery info
      for key, label in widgets['labels'].items():
         value = source_data.get(key, '--')
         if isinstance(value, (int, float)):
            if key in ['voltage', 'current', 'power']:
               text = f"{value:.2f}"
            elif key == 'temperature':
               text = f"{value:.1f}"  
            elif key == 'battery_level':
               text = f"{value:.1f}"
            elif key in ['vmax', 'vmin', 'vdelta', 'battery_nominal_voltage']:
               text = f"{value:.3f}"
            elif key == 'remaining_capacity_mah':
               text = f"{value:.0f}"
            else:
               text = str(value)
         elif isinstance(value, bool):
            text = "On" if value else "Off"
         elif key == 'sources_str':
            # Handle sources array for unified battery
            if 'sources' in source_data:
               text = ', '.join(source_data['sources'])
               if self.debug_mode:
                  print(f"[DEBUG] Sources found: {source_data['sources']}")
            else:
               text = source_data.get('sources_str', '--')
               if self.debug_mode:
                  print(f"[DEBUG] No sources array, using sources_str: {text}")
         else:
            text = str(value) if value else '--'
         
         # Apply special styling for state
         if key == 'charging_state':
            if text.lower() == 'charging':
               label.config(text=text, foreground='#44ff44')
            elif text.lower() == 'discharging':
               label.config(text=text, foreground='#ffaa00')
            elif text.lower() == 'idle':
               label.config(text=text, foreground='#aaaaaa')
            else:
               label.config(text=text, foreground='#ffffff')
         else:
            label.config(text=text, foreground='#ffffff')
      
      # Update battery progress bar
      battery_level = source_data.get('battery_level', 0)
      if isinstance(battery_level, (int, float)) and 'progress' in widgets:
         widgets['progress']['value'] = battery_level
         widgets['percent_label'].config(text=f"{battery_level:.1f}%")
         
         # Change color based on level
         if battery_level <= 20:
            style = 'Critical.TLabel'
         elif battery_level <= 40:
            style = 'Warning.TLabel'
         else:
            style = 'Normal.TLabel'
         widgets['percent_label'].config(style=style)
      
      # Update cell voltages if present and section exists
      if 'cells_tree' in widgets:
         self.update_cells_display(widgets['cells_tree'], source_data)
      elif source_data.get('cells'):
         # Create cells section dynamically if data becomes available
         tab_frame = self.battery_tabs[source_name]
         self.create_cells_section(tab_frame, widgets)
         self.update_cells_display(widgets['cells_tree'], source_data)
      
      # Update temperature sensors if present and section exists
      if 'temps_tree' in widgets:
         self.update_temps_display(widgets['temps_tree'], source_data)
      elif source_data.get('temperatures'):
         # Create temps section dynamically if data becomes available
         tab_frame = self.battery_tabs[source_name]
         self.create_temperatures_section(tab_frame, widgets)
         self.update_temps_display(widgets['temps_tree'], source_data)
      
      # Update faults if present and section exists
      if 'faults_text' in widgets:
         self.update_faults_display(widgets['faults_text'], source_data)
      elif (source_data.get('critical_faults') or source_data.get('warning_faults') or source_data.get('faults')):
         # Create faults section dynamically if data becomes available
         tab_frame = self.battery_tabs[source_name]
         self.create_faults_section(tab_frame, widgets)
         self.update_faults_display(widgets['faults_text'], source_data)
   
   def update_cells_display(self, cells_tree, source_data):
      """Update cell voltages display"""
      cells = source_data.get('cells', [])
      if cells:
         # Clear existing items
         for item in cells_tree.get_children():
            cells_tree.delete(item)
         
         # Add cell data
         for cell in cells:
            cell_num = cell.get('index', 0)
            voltage = cell.get('voltage', 0)
            balance = "Yes" if cell.get('balance', False) else "No"
            status = cell.get('cell_status', 'NORMAL')
            
            # Color code based on status
            tags = []
            if status == 'CRITICAL':
               tags = ['critical']
            elif status == 'WARNING':
               tags = ['warning']
            
            cells_tree.insert('', 'end', text=f"Cell {cell_num:2d}",
                             values=(f"{voltage:.3f}", balance, status),
                             tags=tags)
         
         # Configure tags for coloring
         cells_tree.tag_configure('critical', foreground='#ff4444')
         cells_tree.tag_configure('warning', foreground='#ffaa00')
   
   def update_temps_display(self, temps_tree, source_data):
      """Update temperature sensors display"""
      temperatures = source_data.get('temperatures', [])
      if temperatures:
         # Clear existing items
         for item in temps_tree.get_children():
            temps_tree.delete(item)
         
         # Add temperature data
         for temp_sensor in temperatures:
            sensor_num = temp_sensor.get('index', 0)
            temperature = temp_sensor.get('temperature', 0)
            
            temps_tree.insert('', 'end', text=f"Sensor {sensor_num}",
                             values=(f"{temperature:.1f}",))
   
   def update_faults_display(self, faults_text, source_data):
      """Update faults display"""
      faults_content = ""
      critical_faults = source_data.get('critical_faults', [])
      warning_faults = source_data.get('warning_faults', [])
      general_faults = source_data.get('faults', [])
      
      # Handle critical faults
      if critical_faults:
         faults_content += "CRITICAL FAULTS:\n"
         for fault in critical_faults:
            faults_content += f"  • {fault}\n"
      
      # Handle warning faults
      if warning_faults:
         faults_content += "WARNING FAULTS:\n"
         for fault in warning_faults:
            faults_content += f"  • {fault}\n"
      
      # Handle general faults (from Daly BMS)
      if general_faults and not (critical_faults or warning_faults):
         faults_content += "ACTIVE FAULTS:\n"
         for fault in general_faults:
            faults_content += f"  • {fault}\n"
      
      if not faults_content:
         faults_content = "No active faults"
      
      faults_text.delete(1.0, tk.END)
      faults_text.insert(1.0, faults_content)
   
   def update_system_display(self):
      """Update system tab display"""
      system_data = self.data.get('system', {})
      
      if self.debug_mode:
         print(f"[DEBUG] System data keys: {list(system_data.keys())}")
      
      # CPU usage
      cpu_usage = system_data.get('cpu_usage', 0)
      if isinstance(cpu_usage, (int, float)):
         self.cpu_progress['value'] = cpu_usage
         self.system_labels['cpu'].config(text=f"{cpu_usage:.1f}%")
      
      # Memory usage
      memory_usage = system_data.get('memory_usage', 0)
      if isinstance(memory_usage, (int, float)):
         self.memory_progress['value'] = memory_usage
         self.system_labels['memory'].config(text=f"{memory_usage:.1f}%")
      
      # Fan data
      fan_rpm = system_data.get('fan_rpm', 0)
      fan_load = system_data.get('fan_load', 0)
      
      if isinstance(fan_rpm, int) and fan_rpm > 0:
         self.system_labels['fan_rpm'].config(text=f"{fan_rpm} RPM")
      else:
         self.system_labels['fan_rpm'].config(text="-- RPM")
      
      if isinstance(fan_load, int) and fan_load >= 0:
         self.system_labels['fan_load'].config(text=f"({fan_load}%)")
      else:
         self.system_labels['fan_load'].config(text="(--%)")
      
      # Update power channels (INA3221)
      power_channels = system_data.get('power_channels', [])
      if self.debug_mode:
         print(f"[DEBUG] Updating power channels: {len(power_channels)} channels found")
      
      if power_channels:
         try:
            # Clear existing items
            for item in self.power_tree.get_children():
               self.power_tree.delete(item)
            
            # Add power channel data
            for channel in power_channels:
               channel_num = channel.get('channel', 0)
               label = channel.get('label', f'Channel {channel_num}')
               voltage = channel.get('voltage', 0)
               current = channel.get('current', 0)
               power = channel.get('power', 0)
               shunt = channel.get('shunt_resistor', 0)
               
               self.power_tree.insert('', 'end', text=label,
                                    values=(f"{voltage:.3f}", f"{current:.3f}", 
                                           f"{power:.3f}", f"{shunt:.6f}"))
               if self.debug_mode:
                  print(f"[DEBUG] Added channel: {label} - {voltage:.3f}V, {current:.3f}A")
         except (AttributeError, tk.TclError) as e:
            if self.debug_mode:
               print(f"[DEBUG] Power tree not ready: {e}")
   
   def on_closing(self):
      """Handle window close event"""
      self.running = False
      if self.mqtt_client:
         self.mqtt_client.loop_stop()
         self.mqtt_client.disconnect()
      self.root.destroy()
   
   def run(self):
      """Start the application"""
      self.root.mainloop()


def main():
   parser = argparse.ArgumentParser(description='OASIS STAT Monitor')
   parser.add_argument('--host', default='localhost', help='MQTT broker host')
   parser.add_argument('--port', type=int, default=1883, help='MQTT broker port')
   parser.add_argument('--topic', default='stat', help='MQTT topic to subscribe to')
   parser.add_argument('--debug', action='store_true', help='Enable debug output for MQTT messages')
   
   args = parser.parse_args()
   
   print(f"Starting OASIS STAT Monitor...")
   print(f"Connecting to MQTT broker: {args.host}:{args.port}")
   print(f"Subscribing to topic: {args.topic}")
   if args.debug:
      print("Debug mode enabled - MQTT messages will be logged")
   
   try:
      monitor = StatMonitor(args.host, args.port, args.topic)
      # Enable debug mode if requested
      if args.debug:
         monitor.debug_mode = True
      monitor.run()
   except KeyboardInterrupt:
      print("\nShutting down...")
   except Exception as e:
      print(f"Error: {e}")
      messagebox.showerror("Error", f"Failed to start monitor: {e}")


if __name__ == "__main__":
   main()
