import os
import sys
import time
import unittest

CURRENT_DIR = os.path.dirname(__file__)
LINK_MGMT_DIR = os.path.abspath(os.path.join(CURRENT_DIR, "..", "link_management"))
if LINK_MGMT_DIR not in sys.path:
    sys.path.insert(0, LINK_MGMT_DIR)

from link_management_common import LinkManagementIntegrationBase, create_link, request
class AnalyticsTimeBuckets(LinkManagementIntegrationBase):
    port=38325
    def test_time_bucket_payload(self):
        create_link(self.port,"anx03","https://example.com")
        request(self.port,"GET","/anx03")
        now = int(time.time())
        status,payload,_=request(
            self.port,"GET",
            f"/api/v1/links/anx03/stats?from=0&to={now + 3600}&bucket=hour"
        )
        self.assertEqual(200,status)
        self.assertIn('"time_buckets"',payload)
        self.assertIn('"bucket_start"',payload)
if __name__=='__main__': unittest.main()
