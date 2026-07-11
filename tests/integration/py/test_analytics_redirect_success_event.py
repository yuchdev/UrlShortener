import os
import sys
import time
import unittest

CURRENT_DIR = os.path.dirname(__file__)
LINK_MGMT_DIR = os.path.abspath(os.path.join(CURRENT_DIR, "..", "link_management"))
if LINK_MGMT_DIR not in sys.path:
    sys.path.insert(0, LINK_MGMT_DIR)

from link_management_common import LinkManagementIntegrationBase, create_link, request
class AnalyticsRedirectSuccessEvent(LinkManagementIntegrationBase):
    port = 38321
    def test_success_updates_stats(self):
        status,_,_=create_link(self.port,"anx01","https://example.com")
        self.assertEqual(201,status)
        status,_,_=request(self.port,"GET","/anx01"); self.assertEqual(302,status)
        now = int(time.time())
        status,payload,_=request(
            self.port,"GET",
            f"/api/v1/links/anx01/stats?from=0&to={now + 3600}&bucket=hour"
        )
        self.assertEqual(200,status)
        self.assertIn('"slug":"anx01"',payload)
        self.assertIn('"total_attempts":1',payload)
if __name__=='__main__': unittest.main()
