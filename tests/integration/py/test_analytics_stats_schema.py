import os
import sys
import time
import unittest

CURRENT_DIR = os.path.dirname(__file__)
LINK_MGMT_DIR = os.path.abspath(os.path.join(CURRENT_DIR, "..", "link_management"))
if LINK_MGMT_DIR not in sys.path:
    sys.path.insert(0, LINK_MGMT_DIR)

from link_management_common import LinkManagementIntegrationBase, create_link, request
class AnalyticsStatsSchema(LinkManagementIntegrationBase):
    port=38323
    def test_schema_fields(self):
        create_link(self.port,"anx02","https://example.com")
        request(self.port,"GET","/anx02")
        now = int(time.time())
        status,payload,_=request(
            self.port,"GET",
            f"/api/v1/links/anx02/stats?from=0&to={now + 3600}&bucket=hour"
        )
        self.assertEqual(200,status)
        for k in ['slug','total_attempts','successful_redirects','time_buckets']:
            self.assertIn(f'"{k}"',payload)
if __name__=='__main__': unittest.main()
