import unittest
from test.integration.link_management.link_management_common import LinkManagementIntegrationBase, create_link, request
class AnalyticsTimeBuckets(LinkManagementIntegrationBase):
    port=38325
    def test_time_bucket_payload(self):
        create_link(self.port,"anx03","https://example.com")
        request(self.port,"GET","/anx03")
        status,payload,_=request(self.port,"GET","/api/v1/links/anx03/stats")
        self.assertEqual(200,status)
        self.assertIn('"created_at"',payload)
if __name__=='__main__': unittest.main()
