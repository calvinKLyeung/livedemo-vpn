import glob, json, statistics
import matplotlib.pyplot as plt

def median_mbps(pattern):
    vals = []
    for path in glob.glob(pattern):
        with open(path) as f:
            data = json.load(f)
        vals.append(data["end"]["sum_received"]["bits_per_second"] / 1e6)
    return statistics.median(vals)

labels = ["direct", "livedemo-vpn", "WireGuard"]
values = [median_mbps("direct_*.json"),
          median_mbps("myvpn_*.json"),
          median_mbps("wg_*.json")]

plt.bar(labels, values)
plt.ylabel("Throughput (Mbit/s)")
plt.title("Median of 5 runs, 10 s each")
plt.savefig("throughput.png", dpi=150, bbox_inches="tight")
print(dict(zip(labels, values)))