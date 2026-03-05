import sys
import pandas as pd
import matplotlib.pyplot as plt

COLS = ['time_s', 'T_in_C', 'T_target_C', 'u', 'P_heat_W', 'door']

def load(source):
    df = pd.read_csv(source, comment='#')
    if df.columns[0] != 'time_s':
        df = pd.read_csv(source, comment='#', names=COLS)
    return df

if len(sys.argv) > 1:
    df = load(sys.argv[1])
else:
    import io
    raw = io.StringIO(sys.stdin.read())
    df = load(raw)

fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 6), sharex=True)

ax1.plot(df['time_s'], df['T_in_C'], label='T_in (°C)', color='tomato')
ax1.axhline(df['T_target_C'].iloc[0], linestyle='--', color='gray', label='Setpoint')
ax1.set_ylabel('Temperature (°C)')
ax1.legend()
ax1.grid(True)

ax2.plot(df['time_s'], df['P_heat_W'], label='P_heat (W)', color='steelblue')
ax2.fill_between(df['time_s'], 0, df['P_heat_W'], alpha=0.2, color='steelblue')
ax2.set_ylabel('Heater power (W)')
ax2.set_xlabel('Time (s)')
ax2.legend()
ax2.grid(True)

plt.tight_layout()
plt.savefig('heater_plot.png', dpi=150)
plt.show()
print("Saved: heater_plot.png")
