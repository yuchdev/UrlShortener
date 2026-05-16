import unittest
from test.integration.link_management.link_management_common import LinkManagementIntegrationBase, create_link, request
class AnalyticsStatsSchema(LinkManagementIntegrationBase):
    port=38323
    def test_schema_fields(self):
        create_link(self.port,"anx02","https://example.com")
        request(self.port,"GET","/anx02")
        status,payload,_=request(self.port,"GET","/api/v1/links/anx02/stats")
        self.assertEqual(200,status)
        for k in ['slug','total_redirects','redirects_24h','redirects_7d']:
            self.assertIn(f'"{k}"',payload)
if __name__=='__main__': unittest.main()
