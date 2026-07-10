import os
import sys
import time
import unittest

CURRENT_DIR = os.path.dirname(__file__)
LINK_MGMT_DIR = os.path.abspath(os.path.join(CURRENT_DIR, "..", "link_management"))
if LINK_MGMT_DIR not in sys.path:
    sys.path.insert(0, LINK_MGMT_DIR)

from link_management_common import LinkManagementIntegrationBase, request
class AnalyticsRedirect404Event(LinkManagementIntegrationBase):
    port=38322
    def test_missing_slug_records_event(self):
        status,_,_=request(self.port,"GET","/missing-stage4")
        self.assertEqual(404,status)
        now = int(time.time())
        status,payload,_=request(
            self.port,"GET",
            f"/api/v1/links/missing-stage4/stats?from=0&to={now + 3600}&bucket=hour"
        )
        self.assertEqual(200,status)
        self.assertIn('"total_attempts":1',payload)
if __name__=='__main__': unittest.main()
