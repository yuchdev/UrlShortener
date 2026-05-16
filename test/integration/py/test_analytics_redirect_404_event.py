import unittest
from test.integration.link_management.link_management_common import LinkManagementIntegrationBase, request
class AnalyticsRedirect404Event(LinkManagementIntegrationBase):
    port=38322
    def test_missing_slug(self):
        status,_,_=request(self.port,"GET","/missing-stage4")
        self.assertEqual(404,status)
if __name__=='__main__': unittest.main()
