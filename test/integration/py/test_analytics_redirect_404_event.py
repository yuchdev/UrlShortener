import os
import sys
import unittest

CURRENT_DIR = os.path.dirname(__file__)
LINK_MGMT_DIR = os.path.abspath(os.path.join(CURRENT_DIR, "..", "link_management"))
if LINK_MGMT_DIR not in sys.path:
    sys.path.insert(0, LINK_MGMT_DIR)

from link_management_common import LinkManagementIntegrationBase, request
class AnalyticsRedirect404Event(LinkManagementIntegrationBase):
    port=38322
    def test_missing_slug(self):
        status,_,_=request(self.port,"GET","/missing-stage4")
        self.assertEqual(404,status)
if __name__=='__main__': unittest.main()
