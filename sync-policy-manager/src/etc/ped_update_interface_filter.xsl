<?xml version="1.0" standalone="yes"?>
<!--
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2007-2008, Juniper Networks, Inc.
 * All rights reserved.
-->
<xsl:stylesheet version="1.0"
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
    xmlns:junos="http://xml.juniper.net/junos/*/junos"
    xmlns:xnm="http://xml.juniper.net/xnm/1.1/xnm"
    xmlns:jcs="http://xml.juniper.net/junos/commit-scripts/1.0">

    <xsl:import href="../import/junos.xsl"/>

    <xsl:variable name="arguments">
        <argument>
            <name>interface</name>
            <description>Interface name</description>
        </argument>
        <argument>
            <name>unit</name>
            <description>Interface unit number</description>
        </argument>
        <argument>
            <name>input</name>
            <description>Input filter</description>
        </argument>
        <argument>
            <name>output</name>
            <description>Output filter</description>
        </argument>
        <argument>
            <name>pfd</name>
            <description>PFD filter</description>
        </argument>
    </xsl:variable>

    <xsl:param name="interface"/>
    <xsl:param name="unit"/>
    <xsl:param name="input"/>
    <xsl:param name="output"/>
    <xsl:param name="pfd"/>

    <xsl:template match="/">
        <op-script-results>

            <xsl:variable name="rollback_config">
                <load-configuration rollback="0"/>
            </xsl:variable>

            <xsl:variable name="del_filter">
                <load-configuration>
                    <configuration>
                        <interfaces>
                            <interface>
                                <name><xsl:value-of select="$interface"/></name>
                                <unit>
                                    <name><xsl:value-of select="$unit"/></name>
                                    <family>
                                        <inet>
                                            <filter delete="delete"/>
                                        </inet>
                                    </family>
                                </unit>
                            </interface>
                        </interfaces>
                    </configuration>
                </load-configuration>
            </xsl:variable>

            <xsl:variable name="update_config">
                <load-configuration>
                    <configuration>
                        <interfaces>
                            <interface replace="replace">
                                <name><xsl:value-of select="$interface"/></name>
                                <unit>
                                    <name><xsl:value-of select="$unit"/></name>
                                    <family>
                                        <inet>
                                            <xsl:if test="$input">
                                                <filter>
                                                    <input-list>
                                                        <xsl:value-of select="$input"/>
                                                    </input-list>
                                                </filter>
                                            </xsl:if>
                                            <xsl:if test="$pfd">
                                                <filter>
                                                    <input-list>
                                                        <xsl:value-of select="$pfd"/>
                                                    </input-list>
                                                </filter>
                                            </xsl:if>
                                            <xsl:if test="$output">
                                                <filter>
                                                    <output>
                                                        <xsl:value-of select="$output"/>
                                                    </output>
                                                </filter>
                                            </xsl:if>
                                        </inet>
                                    </family>
                                </unit>
                            </interface>
                        </interfaces>
                    </configuration>
                </load-configuration>
            </xsl:variable>

            <xsl:variable name="lock_result" select="jcs:invoke('lock-configuration')"/>

            <xsl:choose>
                <xsl:when test="$lock_result//message">
                    <xnm:error>
                        <message>
                            <xsl:value-of select="$lock_result//message"/>
                        </message>
                    </xnm:error>
                </xsl:when>

                <xsl:otherwise>
                    <xsl:variable name="del_filter_result" select="jcs:invoke($del_filter)"/>
                    <xsl:variable name="update_result" select="jcs:invoke($update_config)"/>
                    <xsl:choose>
                        <xsl:when test="$update_result//message">
                            <xnm:error>
                                <message>
                                    <xsl:value-of select="$update_result//message"/>
                                </message>
                            </xnm:error>
                            <xsl:variable name="rollback_result" select="jcs:invoke($rollback_config)"/>
                        </xsl:when>

                        <xsl:otherwise>
                            <xsl:variable name="commit_result" select="jcs:invoke('commit-configuration')"/>
                            <xsl:if test="$commit_result//message">
                                <xnm:error>
                                    <message>
                                        <xsl:value-of select="$commit_result//message"/>
                                    </message>
                                </xnm:error>
                                <xsl:variable name="rollback_result" select="jcs:invoke($rollback_config)"/>
                            </xsl:if>
                        </xsl:otherwise>

                    </xsl:choose>
                    <xsl:variable name="unlock_result" select="jcs:invoke('unlock-configuration')"/>
                </xsl:otherwise>
            </xsl:choose>

        </op-script-results>
    </xsl:template>

<!--
                            <output>Update configuration succeeded!
                            </output>
                            <output>
                                  <xsl:text>Add </xsl:text>
                                  <xsl:value-of select="$input"/>
                                  <xsl:text> and </xsl:text>
                                  <xsl:value-of select="$output"/>
                                  <xsl:text> to </xsl:text>
                                  <xsl:value-of select="$interface"/>
                                  <xsl:text> </xsl:text>
                                  <xsl:value-of select="$unit"/>
                                  <xsl:text>.</xsl:text>
                            </output>
-->
</xsl:stylesheet>
