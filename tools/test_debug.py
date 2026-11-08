"""PPSSPP's Vulkan screenshot is BGRA; decode its format before comparing game colours."""
import base64
import unittest

from psp_debug import screenshot_image


class Screenshot(unittest.TestCase):
    def test_colour_order_and_orientation(self):
        for format, data in (("B8G8R8A8_UNORM", b"\x00\x00\xff\xff\xff\x00\x00\xff"),
                             ("R8G8B8A8_UNORM", b"\xff\x00\x00\xff\x00\x00\xff\xff")):
            reply = dict(format=format, width=1, height=2, base64=base64.b64encode(data), flipped=True)
            with self.subTest(format=format):
                image = screenshot_image(reply)
                self.assertEqual(image.getpixel((0, 0)), (0, 0, 255))
                self.assertEqual(image.getpixel((0, 1)), (255, 0, 0))


if __name__ == "__main__":
    unittest.main()
