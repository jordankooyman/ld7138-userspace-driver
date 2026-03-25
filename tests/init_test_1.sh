# Not a Bash Script, but a set of manual checks to perform before running the Python test scripts.
# 1. Confirm SPI is enabled
grep -i spi /boot/firmware/config.txt   # RPiOS Bookworm path
# or: grep -i spi /boot/config.txt      # older RPiOS

# 2. Confirm spidev device exists
ls -la /dev/spidev*
# Expected: /dev/spidev0.0  (and /dev/spidev0.1)

# 3. Confirm gpiochip is present
gpiodetect
# Expected: gpiochip0 [pinctrl-bcm2835] (54 lines)

# 4. Check BCM 24 and 25 are not claimed by anything else
gpioinfo gpiochip0 | grep -E "line 24|line 25"
# Expected: both show "unused"

# 5. Install dependencies if not already done
sudo apt install -y libgpiod-dev libgpiod2 gpiod python3-spidev python3-gpiod