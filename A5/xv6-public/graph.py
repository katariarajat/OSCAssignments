import matplotlib.pyplot as plt

# use statically defined values lol
# each row represents queue values of a particular process
procTimestamps = [
]

plt.yticks([0,1,2,3,4])
for x in procTimestamps:
    plt.plot(x, linestyle='-', marker='o')
plt.show()
