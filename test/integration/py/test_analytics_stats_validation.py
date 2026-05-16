import unittest
from test.integration.link_management.link_management_common import LinkManagementIntegrationBase, request
class AnalyticsStatsValidation(LinkManagementIntegrationBase):
    port=38324
    def test_unknown_slug_policy(self):
        status,_,_=request(self.port,"GET","/api/v1/links/nope/stats")
        self.assertIn(status,[200,404])
if __name__=='__main__': unittest.main()
