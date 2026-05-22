import os
import sys
import unittest

CURRENT_DIR = os.path.dirname(__file__)
LINK_MGMT_DIR = os.path.abspath(os.path.join(CURRENT_DIR, "..", "link_management"))
if LINK_MGMT_DIR not in sys.path:
    sys.path.insert(0, LINK_MGMT_DIR)

from link_management_common import LinkManagementIntegrationBase, request
class AnalyticsStatsValidation(LinkManagementIntegrationBase):
    port=38324
    def test_unknown_slug_policy(self):
        status,_,_=request(self.port,"GET","/api/v1/links/nope/stats")
        self.assertIn(status,[200,404])
if __name__=='__main__': unittest.main()
